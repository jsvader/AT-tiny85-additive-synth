/*
 * Currently, this compiles to 5948 of 6012 bytes of flash, and uses 463 of 512 bytes
 * of dynamic memory. This leaves 51 for the program itself. Changing the sequencer to
 * 64 bytes caused us to run out of memory, hence 48 steps. This is a lucky coincidence
 * anyway, as it is divisible by 3 and 4, so sequences of triplets of semiquavers can
 * be defined. 6 saved patches use 81 bytes each, totalling 486 bytes.
 */

/*
 * typedef because I am too lazy to keep typing unsigned char :)
 */
typedef unsigned char uc;

/*
 * Delay macros. We use timer 1, so we can't use the normal delay/msec
 */
#define sdly(x) \
        while ((msec>>5) < (x)); \
        tval = msec = 0

#define SAMPLES 60
#define SEQ_NO 53

/*
 * Variables used in interrupts. Make them volatile to be more efficient
 */
volatile int sample = 0;
volatile uc bank = 0;
volatile uc newbank = 0;
volatile uc tval = 0;
volatile unsigned int msec = 0;
volatile uc newOctave = 1;
volatile uc newPitch = 255;
volatile uc rnd = 0;
volatile uc update = 0;
volatile uc sndval = 127;

/*
 * General variables for gate, vibrato and debug etc. We define them globally
 * to save on stack size passing to functions.
 */
uc gate = 0;
uc dgate = 0;
uc debug = 0;
char lfo_val = 0;
int lfo_onset = 0;
uc rate_snh = 0;
uc quant = 0;
uc slide = 0;
int pot;
char tune = 0;
char tv = 0;
uc s_on = 0;
uc s_upto = 0;
uc s_learn = 0;
uc s_extra = 0;
uc s_note = 0;
uc s_next = 0;


/*
 * For convenience, from the ADC, we read 204 per ovtace, and divide by 4. This gives 51 per octave.
 * 255 sounds better than other combinations, especially for low notes.
 */
const uc noteLookup[51] = {255,252,248,245,242,238,235,232,229,226,223,220,217,214,211,209,206,203,200,198,195,192,190,187,185,182,180,178,175,173,171,168,166,164,162,160,157,155,153,151,149,147,145,143,141,140,138,136,134,132,130};
const uc quantised[12] = {255,241,227,214,202,191,180,170,160,151,143,135};

/*
 * We create a sound sample of length 60, because it is divisible by 1-6. We always start at 0 for each wave.
 * We have 2 banks so we can setup the new one and switch at the correct time.
 * 
 * For performance, we cache the SINE wave, also to 60 samples.
 */
volatile uc snd[2][SAMPLES];
volatile uc *sbank = snd[0];
char sine[SAMPLES] = {0,13,26,39,51,63,74,84,94,102,109,115,120,124,126,127,126,124,120,116,109,102,94,85,74,63,51,39,26,13,0,-12,-26,-38,-51,-63,-74,-84,-94,-102,-109,-115,-120,-124,-126,-127,-126,-124,-120,-116,-110,-102,-94,-85,-74,-63,-51,-39,-26,-13};

/*
 * The current wave and env settings. Dont define here to save space.
 */
unsigned int env[6][5]; // = { { 0, 0, 0, 60, 240 }, { 0, 0, 0, 60, 240 }, { 0, 0, 0, 60, 240 }, { 0, 0, 0, 60, 240 }, { 0, 0, 0, 60, 240 }, { 0, 0, 240 } };
char wave[6][2]; // = { { 60, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };
uc lfo[3]; // type, rate, onset 
uc special[6]; // lfo pitch amount, lfo repeat, lfo repeat duty, snh pitch amount, snh rate amount - sixth not yet defined

/*
 * The current envelope settings. This represents the volume of the harmonic, and the state it is in.
 */
int filt[6] = { 0, 0, 0, 0, 0, 0 };
uc filt_state[6] = { 0, 0, 0, 0, 0, 0 };

/*
 * Sequencer 16 notes. Note values store the octave and the semitone. Each octave
 * adds 12 to the note value. There are 12 semitones per octave. A value of 37 would
 * be octave = 3, note = 1. Values above 0xf0 are reserved for a rest or end of
 * sequence. Eventually, a held note will appear here too.
 * 
 * value = semitone + octave * 12
 */
uc sequencer[SEQ_NO];

/*
 * Write a blob to EEPROM. Write len bytes of data to addr.
 */
void EEPROM_write(unsigned short addr, uc *data, uc len)
{
    cli();
    for (uc i = 0; i < len; i++, data++, addr++) {
        /* Wait for completion of previous write */
        while(EECR & (1<<EEPE));
        /* Set Programming mode */
        EECR = (0<<EEPM1)|(0<<EEPM0);
        /* Set up address and data registers */
        EEAR = addr;
        EEDR = *data;

        /* Write logical one to EEMPE */
        EECR |= (1<<EEMPE);
        /* Start eeprom write by setting EEPE */
        EECR |= (1<<EEPE);
    }
    while(EECR & (1<<EEPE));
    sei();
}

/*
 * Read a blob from EEPROM. Read len bytes from addr to data. 
 */
void EEPROM_read(unsigned short addr, uc *data, uc len)
{
    cli();
    for (int i = 0; i < len; i++, data++, addr++) {
        /* Wait for completion of previous write */
        while(EECR & (1<<EEPE));
        /* Set up address register */
        EEAR = addr;
        /* Start eeprom read by writing EERE */
        EECR |= (1<<EERE);
        /* Return data from data register */
        *data = EEDR;
    }
    sei();
}

/*
 * Update the current sample. Write to the bank not in use, then set the
 * flag so that the interrupt routine will switch when we get back to 0.
 */
inline void create_wave(uc b) {
    int result = 0;

    /*
     * initialise the phase of the wave
     */
    uc v[6] = {0, wave[1][1], wave[2][1], wave[3][1], wave[4][1], wave[5][1]};
    uc pval = 0;
    for (int i = 0; i < SAMPLES; i++) {
        /*
         * FM synth engine. 2 Operator with feedback on the carrier
         */
        if (wave[1][1] > 0x40) {
            /*
             * val is the next index for the modulated carrier
             */
            int val = ((i << 8) + ((filt[1] >> 8) * (int)sine[v[0]])) >> 8;
            /*
             * Apply feedback
             */
            val += ((filt[2] >> 8) * (int)sine[pval]) >> 8;
            /*
             * Sanitise from 0-59 (SAMPLES)
             */
            while (val >= SAMPLES) val -= SAMPLES;
            while (val < 0) val += SAMPLES;
            /*
             * store the old value of the carrier index for use in feedback
             */
            pval = val;
            /*
             * Update the modulator and sanitize 0-59
             */
            v[0] += wave[1][1] & 0xf;
            while (v[0] >= SAMPLES) v[0] -= SAMPLES;
            while (v[0] < 0) v[0] += SAMPLES;
            /*
             * The result is the envelope of the carrier
             */
            result = (filt[0] >> 8) * (int)sine[val];
        /*
         * Additive synth engine. Changed to a loop rather than multiple lines
         * with multiplies in them. The ATTiny85 doesn't have a mult instruction
         * so multiplies use quite a but of space. This alone saved almost 1/2K
         * of flash. Also note a slight bug before - we updated the waves *before*
         * using them, so everything bar the fundemental was out by 1 index value
         * ie. a phase shift of 1.
         */
        } else {
            result = 0;
            for (int j = 0; j < 6; j++) {
                result += (filt[j] >> 8) * (int)sine[v[j]];
                v[j] += (j+1);
                if (v[j] >= SAMPLES) v[j] -= SAMPLES;
            }
        }
        snd[b][i] = (uc)((result + 32512) >> 8); // snd is unsigned so shift so that -128 -> 127 => 0 -> 255
    }
}

/*
 * Load, save or initialise one of the wave stores from/to EEPROM.
 */
inline void preset(uc num, uc fn) {
    unsigned short offset = (sizeof(wave) + sizeof(env) + sizeof(special) + sizeof(lfo)) * ((unsigned short)num-1) + 2;
    /*
     * The compiler doesn't like casting the 2d array to a normal char *, so be obtuse.
     */
    uc *w = (uc *)(&wave[0][0]);
    uc *e = (uc *)(&env[0][0]);
    uc *s = (uc *)(&special[0]);
    uc *l = (uc *)(&lfo[0]);

    switch(fn) {
        case 0: // default
            memset(wave, 0, sizeof(wave));
            memset(env, 0, sizeof(env));
            memset(lfo, 0, sizeof(lfo));
            memset(special, 0, sizeof(special));

            /*
             * Simple sine wave, max vol, instant attack, max sustain, quick release
             */
            wave[0][0] = 60;
            env[0][2] = 240;
            env[0][3] = env[1][3] = env[2][3] = env[3][3] = env[4][3] = env[5][3] = 255;

            /*
             * 50% duty cycle for LFO key repeat
             */
            special[2] = 64;

            /*
             * LFO type square, about 3Hz, instant onset
             */
            lfo[0] = 1;
            lfo[1] = 5;
            lfo[2] = 0; // instant onset            
            break;
            
        case 1: // save
            EEPROM_write(offset, w, sizeof(wave));
            offset += sizeof(wave); 
            EEPROM_write(offset, e, sizeof(env));                 
            offset += sizeof(env); 
            EEPROM_write(offset, s, sizeof(special));        
            offset += sizeof(special); 
            EEPROM_write(offset, l, sizeof(lfo));        
            break;
        
        case 2: // load
            EEPROM_read(offset, w, sizeof(wave));
            offset += sizeof(wave); 
            EEPROM_read(offset, e, sizeof(env));
            offset += sizeof(env); 
            EEPROM_read(offset, s, sizeof(special));        
            offset += sizeof(special); 
            EEPROM_read(offset, l, sizeof(lfo));        
            break;
            
            default: // should never get here
                break;
        }
}

/*
 * Called every 10ms, this updates the individual harmonic volumes in line with
 * their Envelope. There are 4 envelope types. These are ADSR, delayed ADSR,
 * AD repeat and delayed AD repeat. The delayed envelope waits to go into the
 * decay phase until all active oscillators have finished their attack phase.
 * 
 * State 0 => attack
 * State 1 => delay
 * State 2 => decay
 * State 3 => sustain
 * State 4 => release
 * State 5 => note off
 * 
 * A standard adsr will go, states: 0, 2, 3, 4, 5
 */
inline void update_envelope() {
    uc finished = 0;
    uc playing = 6;
    int attack, decay, level, sustain, rel;
    uc etype;
    uc tremolo = 1;

    /*
     * Work out which notes are still playing, and which have
     * finished their attack phase. Repeating envelopes don't
     * count towards a playing note as the repeat indefinitely.
     */
    for (uc i = 0; i < 6; i++) {
        if (env[i][0] < 2 && wave[i][0])
            tremolo = 0;
        if (filt_state[i] > 0 || env[i][0] > 1 && env[i][0] < 4) {
            finished++;
            if (filt_state[i] == 6 || env[i][0] > 1)
                playing--;
        }
    }

    /*
     * Loop through the oscillators, and set their filt value. The
     * filt value represents the current volume of the oscillator.
     */
    for (uc i = 0; i < 6; i++) {
        level = ((int)wave[i][0]) << 8;
        if (level == 0) {
            filt_state[i] = 6;
            continue;
        }
        etype = (uc)(env[i][0]);
        attack = env[i][1];
        decay = env[i][2];
        sustain = (level >> 8) * env[i][3];
        rel = env[i][4];
        switch (filt_state[i]) {
            case 0: // Attack
                filt[i] += attack;
                if (attack == 0 || filt[i] >= level) {
                    filt[i] = level;
                    filt_state[i] = (etype & 1) ? 1 : 2;
                }
                break;
            case 1: // Delay
                if (finished == 6)
                    filt_state[i] = 2;
                else
                    break;
            case 2: // Decay
                filt[i] -= decay;
                if (decay == 0 || filt[i] <= sustain) {
                    filt[i] = sustain;
                    if (etype > 1 && etype < 4) {
                        if (playing || tremolo)
                            filt_state[i] = 0;
                        else
                            filt_state[i] = 6;
                    } else {
                        filt_state[i] = 3;
                    }
                }
                break;
            case 3: // Sustain stay here until told to release
                if (filt[i] == 0)
                    filt_state[i] = 6;
                break;
            case 4: // Release
                if (etype == 4) {
                    filt[i] += rel;
                    if (rel == 0 || filt[i] >= level) {
                        filt[i] = level;
                        filt_state[i] = 5;
                    }
                } else {
                    filt[i] -= rel;
                    if (rel == 0 || filt[i] <= 0) {
                        filt[i] = 0;
                        filt_state[i] = 6;
                    }
                }
                break;
            case 5: // Reverse Release
                filt[i] -= decay;
                if (rel == 0 || filt[i] <= 0) {
                    filt[i] = 0;
                    filt_state[i] = 6;
                }
                break;
            default: // Note off
                filt[i] = 0; // take care of repeating oscillators
                break;
        }
    }
}

/*
 * Calculate the SINE wave, set the I/Os, setup the timers.
 */
void setup() {
    preset(0,0);
    create_wave(0);
    newPitch = 240;
    sequencer[0] = 24;
    sequencer[1] = 0xff;
    EEPROM_read(quant, (uc *)&tv, 1); // read tuning value


    //pinMode(0, OUTPUT);
    DDRB = 1 << DDB0;                       
    TCCR0A = 1 << COM0A1 | 1 << WGM01  | 1 << WGM00; // fasm pwm
    TCCR0B = B00000001;        // prescalar to 8
    OCR0B = 255;

    TCCR1 = 0;                                        // Stop timer
    TCNT1 = 0;                                        // Zero timer
    GTCCR = _BV(PSR1);        // Reset prescaler
    OCR1A = newPitch;
    OCR1C = newPitch;

    /*
     * Start timer in CTC mode
     */
    TCCR1 = _BV(CTC1) | _BV(CS11);

    sei();
    TIMSK |= (1 << OCIE0B) | (1 << TOIE1) | (1 << OCIE1A);

    /*
     * This sets up the PWM. We could probably save space by implementing the setup.
     */
    //analogWrite(0, 128);
    OCR0A = 128;
}

/*
 * Sample advancing interrupt. This provides the pitch of the note.
 */
ISR(TIMER1_COMPA_vect) {
    if (update) {
        OCR1A = OCR1C = newPitch;
        TCCR1 = _BV(CTC1) | (((newOctave & 0x30)>>4) + 2);
        bank = newbank;
        sbank = snd[bank];
        update = 0;
    }

    sample += (newOctave >> 6);
    if (sample >= SAMPLES)
        sample = 0;
    sndval = *(sbank + sample);
    /*
     * pseudo random number generator. Add one each time the interrup triggers.
     * We have 2 interrupts which aren't in any way synchronised, so the value
     * doesn't follow a simple pattern. It sounds pretty random to me.
     */
    rnd++;
}

/*
 * PWM interrupt. 33kHz 8 bit resolution. This also maintains the millisecond
 * timer as it is a constant frequency. We use phase correct PWM, so the clock
 * is 8.25MHz. 32 cycles is *approx* 1ms. It probably should be 33, but 32 is
 * a much nicer number. Make this routine as small as possible so that the sound
 * is less noisy.
 */
ISR(TIMER0_COMPB_vect) {
    OCR0A = sndval;
    msec++;
    rnd++;
}

/*
 * Generate the pitch information, as well as octave information from the
 * ADC output value. If the LFO is enabled, adjust the pitch accordingly.
 * 
 * ***NOTE*** getNote uses the most RAM of any routine. With a 48 note
 * sequencer, we use exactly 512 bytes of RAM whilst inside getNote. If
 * any more RAM is used, the synth will crash.
 */
inline void getNote(int val) {
    static int vib = 0;
    static uc vib_dir = 1;
    static int prev = 0;
    static int pnote = -1;
    int delta = 0;
    uc not_sliding = 1;
    uc diff = 255;
    uc cur;
    uc note = 0;

    /*
     * Each microtone is 4 apart. To stop noise on the CV causing 
     * the pitch to jump and sound noisy, we only update when we have
     * changed 2 microtones or more. The LFO and slide is uneffected
     * by this. If we are sliding, don't
     */
    if (pnote >= 0 && (val - pnote) < 8 && (val - pnote) > -8)
      val = pnote;
    else
      pnote = val;

    if (special[0]) { // ie. not zero
        delta = ((int)lfo_val * (int)special[0]) >> 7;
        delta = (delta * ((lfo_onset + 1) >> 2)) >> 8;
    }

    val += delta;
    if (val < 0) val = 0;
    else if (val > 1023) val = 1023;

    /*
     * Slide (portamento)
     */
    if (slide > 0) {
        if (val < prev) {
            prev -= slide<<2;
            if (prev < val) prev = val; 
        } else if (val > prev) {
            prev += slide<<2;
            if (prev > val) prev = val;
        }
        not_sliding = (val == prev);
        val = prev;
    } else {
        prev = val;
    }
        
    /*
     * 1 octave = 1V, 5V = 1024, so 1V is about 204 give or take.
     */
    if (val < 204) {
        newOctave = 0x70;
    } else if (val < 408) {
        newOctave = 0x60;
        val -= 204;
    } else if (val < 612) {
        newOctave = 0x50;
        val -= 408;
    } else if (val < 816) {
        newOctave = 0x40;
        val -= 612;
    } else {
        newOctave = 0x80;
        val -= 816;
    }

    /*
     * Lookup the 1V/octave, logrithmic pitch info. There are 51 steps per octave,
     * so around 4 divisions per semitone.
     */
    newPitch = noteLookup[val >> 2];

    /*
     * If we quantize, go to the nearest semitone. Dont quantise while sliding.
     */
    for (uc i = val >> 6; i < 12; i++) {
        cur = (newPitch < quantised[i]) ? quantised[i] - newPitch : newPitch - quantised[i];
        if (cur < diff) {
            note = i;
            diff = cur;
        } else {
          break;
        }
    }
    s_note = newOctave + note;
    if (quant && not_sliding)
        newPitch = quantised[note];
}

/*
 * Button logic. We can have a short or long press on each button. Determine
 * the function from the button state and long press state. This is a state machine.
 * State 0 => awaiting button press
 * State 1 => Attack
 * State 2 => Release
 * State 3 => Envelope / Sustain
 * State 4 => Harmonic volume
 * State 5 => Harmonic phase
 * State 6 => Load sound preset (saved sound)
 * State 7 => Sustain
 * State 8 => Decay
 * State 9 => LFO
 * State 10 => Tune
 * State 11 => Special (patch matrix)
 * State 12 => Save/default preset
 * 
 */
inline uc do_buttons(uc button, uc long_press) {
    static uc button_state = 0;
    static uc current_osc = 0;
    static uc preset_press = 0;
    static uc sub_button = 0;
    const int env_values[6] = {1, 10, 20, 40, 80, 160};

    if (tune && button == 110) // save tuning
        EEPROM_write(quant, (uc *)&tv, 1);

    tune = 0;
    if (button > 100) {
        sub_button = 0;
        if (button - 106 == button_state || (button - 100) == button_state)
            goto finish;
        button_state = 0;
        goto value;
    }
        
    switch(button_state) {
        /*
         * Select function, osc or slide
         */
        case 0:
            if (button < 7) {
                if (s_on) {
                    s_next = (s_next == button) ? 0 : button;
                    goto finish;
                }
                if (long_press) {
                    slide = (button == 1) ? 0 : 7 - button;
                    goto finish;
                }
                current_osc = button - 1;
                goto finish;
            } else {
                if (long_press)
                    button_state = button;
                else
                    button_state = button - 6;
                goto value;
            }

        /*
         * Attack
         */
        case 1:
            if (button < 7) {
                /*
                 * Long pressing the value buttons on attack adds a template for the envelopes
                 * 
                 * The predefined ones are:
                 * 1) Pluck with long delay/release
                 * 2) Pluck with short delay/release
                 * 3) Pluck with very short delay/release
                 * 4) Sweeping low pass filter short attack/release (used for the trumpet sound)
                 * 5) Sweeping low pass filter medium attack/release
                 * 6) Sweeping low pass filter with long attack/release (pads)
                 */
                if (long_press) {
                    unsigned int d0 = 0, d1, d2, d3, d4, d5;
                    switch(button) {
                        case 1:
                            d0 = 1;
                            d1 = 1;
                            d2 = 0;
                            d3 = 201;
                            d4 = 40;
                            d5 = 0;
                            break;
                        case 2:
                            d0 = 1;
                            d1 = 1;
                            d2 = 0;
                            d3 = 101;
                            d4 = 20;
                            d5 = 0;
                            break;
                        case 3:
                            d0 = 1;
                            d1 = 1;
                            d2 = 0;
                            d3 = 16;
                            d4 = 3;
                            d5 = 0;
                            break;
                        case 4:
                            d1 = 5;
                            d2 = 5;
                            d3 = 31;
                            d4 = 5;
                            d5 = 256;
                            break;
                        case 5:
                            d1 = 20;
                            d2 = 20;
                            d3 = 61;
                            d4 = 10;
                            d5 = 256;
                            break;
                        case 6:
                            d1 = 40;
                            d2 = 40;
                            d3 = 241;
                            d4 = 40;
                            d5 = 256;
                            break;
                    }
                    for (int i = 0; i < 6; i++) {
                        env[i][0] = d0;
                        env[i][1] = (((int)wave[i][0])<<8) / d1;
                        env[i][2] = (((int)wave[i][0])<<8) / d3;
                        env[i][3] = d5;
                        env[i][4] = (((int)wave[i][0])<<8) / d3;
                        d1 += d2;
                        d3 -= d4;
                    }
                    goto finish;
                }
                env[current_osc][1] = (((int)wave[current_osc][0])<<8) / env_values[button-1];
                goto finish;
            }
            env[current_osc][1] = pot;
            goto value;

        /*    
         * Release
         */
        case 2:
            if (button < 7) {
                env[current_osc][4] = (((int)wave[current_osc][0])<<8) / env_values[button-1];
                goto finish;
            }
            env[current_osc][4] = pot;
            goto value;

        /*
         * Envelope
         */
        case 3:
            if (button < 7) {
                env[current_osc][0] = button - 1;

                /* remove once more envelopes are defined */
                if (button > 5)
                    env[current_osc][0] = 0;
                    
                goto finish;
            }
            goto wait;

        /*
         * Osc amplitude
         */
        case 4:
            if (button < 7) {
                if (long_press) {
                  memcpy(wave, 0, sizeof(wave));
                  uc *tmp = (uc *)wave;
                  switch(button) {
                      case 1: // saw
                          *tmp = 60;
                          *(tmp + 2) = 30;
                          *(tmp + 4) = 20;
                          *(tmp + 6) = 15;
                          *(tmp + 8) = 12;
                          *(tmp + 10) = 10;
                          break;
                      case 2: // square
                          *tmp = 60;
                          *(tmp + 4) = 20;
                          *(tmp + 8) = 12;
                          break;
                      case 3: // triangle
                          *tmp = 60;
                          *(tmp + 4) = 6;
                          *(tmp + 5) = 10;
                          *(tmp + 8) = 3;
                          break;
                      case 4: //pulse (all harmonics equal)
                          *tmp = 30;
                          *(tmp + 2) = 30;
                          *(tmp + 4) = 30;
                          *(tmp + 6) = 30;
                          *(tmp + 8) = 30;
                          *(tmp + 10) = 30;
                          break;
                      case 5: // reverse saw (resonant sounding pulse)
                          *tmp = 10;
                          *(tmp + 2) = 20;
                          *(tmp + 4) = 30;
                          *(tmp + 6) = 40;
                          *(tmp + 8) = 50;
                          *(tmp + 10) = 60;
                          break;
                      case 6: // saw + 1/2 volume square
                          *tmp = 60;
                          *(tmp + 2) = 60;
                          *(tmp + 4) = 20;
                          *(tmp + 6) = 15;
                          *(tmp + 8) = 12;
                          *(tmp + 10) = 20;
                          break;
                  }
                } else {
                  wave[current_osc][0] = 60 / button;
                }
                goto finish;
            }
            wave[current_osc][0] = (uc)(pot>>4);
            goto value;

        /*    
         * Osc phase / sequence
         */
        case 5:
            switch(button) {
                case 1:
                    if (sub_button) {
                        wave[1][1] = 0x40 + ((pot >> 7) + 1);
                        goto value;
                    } else if (long_press) {
                        sub_button = 1;
                        goto value;
                    } else { // phase = 0
                        wave[current_osc][1] = 0;
                        goto finish;
                    }

                case 2: // phase = 180 degrees
                    wave[current_osc][1] = 30 / (current_osc+1);
                    goto finish;

                case 3: // play/stop sequence
                    s_on = s_on ? 0 : (gate == 4) ? 2 : 1;
                    s_upto = 255;
                    s_next = 0;
                    update_lfo(1);
                    if (!s_on)
                        memset(filt_state, 4, 6);
                    goto finish;

                case 4: // Start/stop sequencer editing
                    s_learn = 1 - s_learn;
                    if (!s_learn) {
                        sequencer[s_upto] = 0xff;
                        goto finish;
                    } else {
                        memset(sequencer, 0, SEQ_NO);
                        s_upto = 0;
                    }
                    goto wait;
                    
                case 5: // rest
                    if (s_learn)
                        sequencer[s_upto++] = 0xfd;
                    goto wait;
                    
                case 6: // hold
                    if (s_learn)
                        sequencer[s_upto++] = 0xfe;
                    goto wait;
            }
            goto wait;

        /*
         * Preset load
         */
        case 6:
#if 0
            if (button == 12)
                debug = 1;
#endif
            if (button < 7) {
                quant = long_press;
                EEPROM_read(quant, (uc *)&tv, 1); // read tuning value    
                preset(button, 2); // load
                goto finish;
            }
            goto wait;

        /*
         * State 7-12 is for long press buttons
         */

        /*
         * Sustain
         */
        case 7:
            if (button < 7) {
                env[current_osc][3] = (button == 6) ? 0 : 256 - (button - 1)* 50;
                goto finish;
            }
            env[current_osc][3] = pot >> 2;
            goto value;

        /*    
         * Decay
         */
        case 8:
            if (button < 7) {
                env[current_osc][2] = (((int)wave[current_osc][0])<<8) / env_values[button-1];
                goto finish;
            }
            env[current_osc][2] = pot;
            goto value;

        /*
         * LFO
         */
        case 9:
            if (button < 5) {
                lfo[0] = button - 1;
                goto finish;
            }
            if (button == 5) {
                lfo[1] = pot >> 4;
            } else if (button == 6) {
                lfo[2] = pot >> 4;
            }
            goto value;
        
        /*    
         * Tune
         */
        case 10:
            tune = 1;
            goto finish;

        /*
         * Special
         */
        case 11:
            switch(button) { // instant, eg quantise, repeat
                case 2: // toggle lfo repeat
                    special[1] = 1 - special[1];
                    goto finish;
                case 6: // toggle quantisation
                    quant = 1 - quant;
                    EEPROM_read(quant, (uc *)&tv, 1); // read tuning value    
                    goto finish;
            }

            switch(button) { //value required
                case 1: // LFO pitch depth
                    special[0] = pot >> 4;
                    break;
                case 3: // LFO duty cycle
                    special[2] = pot >> 4;
                    break;
                case 4: // SNH mod amount
                    special[3] = pot >> 2; // about 1 octave max.
                    break;
                case 5: // SNH LFO rate amount
                    special[4] = pot >> 4; // max amount
                    break;
            }
            goto value;

        /*
         * Preset save / default sound
         */
        case 12:
            if (button < 7) {
                if (long_press)
                    preset(button, 0); // default
                else
                    preset(button, 1); // save
                goto finish;
            }
            goto wait;

        default:
            goto finish;
        }

/*
 * button press required, don't keep polling
 */
wait:
    return 0;

/*
 *    value set, don't keep polling
 */
finish:
    button_state = 0;
    return 0;

/*
 * poll for new value from pot, or button
 */
value:
    return 1;
}

/*
 * Update the lfo_value in line with type, rate and onset.
 * There are 4 LFO types: square, triangle, saw down and 
 * saw up. The LFO rate and onset time can be adjusted. The
 * snh value can be used to adjust the LFO rate.
 */
void update_lfo(uc reset) {
    static uc lfo_count = 0;
    uc new_val = 0;

    if (reset)
        lfo_count = 0;
        
    if (!lfo[1]) {
        lfo_val = 0;
        return;
    }
    switch(lfo[0]) {
        case 0: // square
            lfo_val = (lfo_count > 127) ? 127 : 0;
            break;
        case 1: // triangle
            if (lfo_count < 128) new_val = lfo_count << 1;
            else new_val = (255 - lfo_count) << 1;
            lfo_val = new_val - 127;
            break;
        case 2: // saw down
            lfo_val = 127 - (lfo_count >> 1);
            break;
        case 3: // saw up
            lfo_val = lfo_count >> 1;
            break;
        default:
            break;
    }
    lfo_count += lfo[1] + rate_snh; // let it wrap
    lfo_onset += lfo[2];
    if (!lfo[2] || lfo_onset > 1023) lfo_onset = 1023;
}

/*
 * Loop through every 10ms, read the inputs (pitch/gate) and update the envelope.
 * 10ms means we update 100 times a second which is enough to fool the ear into
 * thinking it is analog.
 */
void loop() {
    static int newpitch = 0;
    static uc button_debounce = 0;
    static int debounce = 0;
    static uc preset_no = 1;
    static uc current_button = 0;
    static uc button_value = 0;
    static uc rpt = 0;
    static int snh = 0;

    #if 0
    /*
     * Notes for the debug option. Tune the resistors till the sequence is achieved.
     * This is #ifed out originally for space. We now have the space, but dont really
     * need this except when first setting up the board.
     */
    uc dodgy[13] = {240, 220, 200, 180, 160, 140, 120, 100, 90, 80, 70, 60, 50};
    #endif
    
    int button = 0;
    uc moctave = 0;
    uc mpitch = 0;

    pot = analogRead(1);
    if (tune)
        tv = (pot >> 4) - 32;

    /*
     * Debounce the gate. Need 4 reads = 3 delays, or 30ms. Disabled if
     * repeat is active or the sequencer is playing in auto mode.
     */
    if (!special[1] && s_on != 1) {
        if (digitalRead(1) != dgate) // gate has changed
            debounce++;
        else
            debounce = 0;
        if (debounce >= 3) {
            debounce = 0;
            dgate = 1 - dgate;
            gate = dgate;
        }
    }

#if 0
    /*
     * Play the debug sound. Cancel if the preset button is pressed again.
     * Same as above - not required except when tuning the board.
     */
    if (debug) {
        if (current_button == 12) {
            debug = 0;
            gate = 0;
            current_button = 0;
        } else {
            gate = 1;
            pitch = dodgy[current_button & 0x7f];
        }
    }
#endif

    /*
     * Read the buttons. Debounce for 40ms (4 iterations). A long press is 50 debounce
     * reads, or about 1/2 a second. The ADC we use is also used by the USB circuitry.
     * It means we have a max of around 3.6V . 620 is around 3V, so if no button is pressed,
     * we read above 620.
     * 
     * The button_value variable determines if we poll every cycle, or wait for a button
     * press. Some fuctions read the pot (ie. polled).
     */
        button = analogRead(3);
        if (button <= 620) {
            button_debounce++;                        
        } else {
            if (button_debounce > 4) {
                if (current_button > 6)
                    button_value = do_buttons(100 + current_button, 0); // clear the button state on a new function button
                else
                    button_value = do_buttons(current_button, button_debounce > 50);
            }
            if (button_value) // polling the buttons for a new value
                button_value = do_buttons(current_button, button_debounce > 50);
            button_debounce = 0;
        }

        /*
         * Set the button number after 4 debounces, but only call the function when the
         * button is released. The values are taken from trial and error. The analogue
         * input has a 1.5K pullup for USB comms. This ends up giving us an odd, log
         * style voltage. Tune these using the debug mode.
         */
        if (button_debounce == 4) {
            if (button > 615)
                current_button = 4;
            else if (button > 610)
                current_button = 5;
            else if (button > 605)
                current_button = 6;
            else if (button > 600)
                current_button = 1;
            else if (button > 590)
                current_button = 2;
            else if (button > 560)
                current_button = 3;
            else if (button > 510)
                current_button = 7;
            else if (button > 450)
                current_button = 8;
            else if (button > 380)
                current_button = 9;
            else if (button > 300)
                current_button = 10;
            else if (button > 210)
                current_button = 11;
            else
                current_button = 12;
        }

    /*
     * Repeating gates from the LFO. The sequencer also uses this
     * functionality. This means you can control the duty cycle of
     * the note from the sequencer.
     */
    if (special[1] || s_on == 1) { // lfo repeating gate is on
        if (gate == 3 && lfo_val >= special[2]) // toggle from low to high
            gate = 1;
        else if (gate == 4 && lfo_val < special[2]) // toggle from high to low
            gate = 0;
    }
    
    /*
     * If we are playing and the gate goes low, immediately go to envelope state 4 (release).
     * If the gate has been triggered, set the envelope to state 0 (attack). Reset the 
     * vibrato onset.
     */
    if (gate == 0) {
        memset(filt_state, 4, 6);
        gate = 3;
        /*
         * Update the rate snh on key up
         */
        rate_snh = (((int)special[4] * (int)rnd) >> 10); // new rate snh
    } else if (gate == 1) {
        memset(filt_state, 0, 6);
        if (lfo[0] > 1)
            update_lfo(1); // reset the LFO for the sawtooth waves
        /*
         * Update the pitch snh on key down
         */
        snh = ((int)rnd * (int)special[3]) >> 8; // new pitch snh
        lfo_onset = 0;

        /*
         * If we are learning the sequence, update the array with the current
         * note from getNote. If we go over the max length, start playing to
         * make sure the user knows we have exhausted the sequencer.
         */
        if (s_learn) {
            sequencer[s_upto++] = s_note;
            if (s_upto >= SEQ_NO) {
                s_learn = 0;
                s_on = 1;
                s_next = 0;
                s_upto = 255;
            }
        /*
         * If the sequencer is running, check if we have set a sub sequence.
         * Repeat if we get to the end of the subsequence, or a new subsequence
         * has been requested.
         */
        } else if (s_on) {
            s_upto++;
        }
        gate = 4;
    }
    update_lfo(0);

    /*
     * If the sequencer is playing, get the next note. Look for sequence end (0xff)
     * and rest 0xfd.
     */
    if (s_on && s_upto != 255) {
        if (s_upto == SEQ_NO || sequencer[s_upto] > 0xfd) {
            /*
             * reset the sequence to either the start of the sequence, or subsequence.
             */
            if (s_next) {
                uc s_num = 1;
                s_upto = 0;
                while(s_upto < SEQ_NO && s_num < s_next) {
                    while(s_upto < SEQ_NO && sequencer[s_upto++] < 0xfe);
                    if (sequencer[s_upto-1] == 0xff) {
                        s_upto = 0;
                        break;
                    }
                    s_num++;
                }                    
                if (s_upto >= SEQ_NO)
                    s_upto=0;
            } else {
                s_upto = 0;
            }
        }
        if (sequencer[s_upto] == 0xfd) {
            /*
             * A rest means go to the release phase. If the notes are already off, this will
             * move directly to the off state. If they are still playing, then this will
             * cause them to stop.
             */
            memset(filt_state, 4, 6);
        } else {
            /*
             * generate pitch and octave from the s_note value.
             */
            newPitch = quantised[sequencer[s_upto] & 0xf];
            newOctave = sequencer[s_upto] & 0xf0;
        }
    } else {
        /*
         * normal CV generated note. Uses the tuning value (tv) and sample an hold (snh)
         */
        getNote(analogRead(2)+tv+snh);
    }

    /*
     * Update the envelope.This is where the magic happens.
     */
    update_envelope();

    /*
     * These routines used to be in the envelope, but that ran us out of memory with the
     * function calls. Makes no difference if they are called there or here, so lets save
     * memory. We are pretty much at 100% mempory utilisation now.
     */
    create_wave(1 - bank);
    newbank = 1 - bank;
    update = 1;

    /*
     * sdly delays from the last time it was called, so it basically ensures the loop
     * runs every 10ms. We need to make sure that the code doesn't take more than 10ms
     * per loop, or we will end up with inconsistent envelopes which makes the output
     * sound noisy.
     */
    sdly(10);
}
