/*
 * Copyright 2002 Karl Bongers (karl@turbobit.com)
 * Copyright 2007,2013 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file
 * for details.
 *
 * -----------
 *
 * the IR receiver should be something like the Panasonic
 * PNA4602M (5V), Sharp GP1UD261XK0F (2.7 - 5.5V), or Sharp
 * GP1UX511QS (5V).
 *
 * refer to the datasheet for your specific part.  connect a 10uf
 * to 50uf cap between Vcc and gnd, very close to the detector.
 *
 * for the Panasonic PNA4602M and Sharp GP1UX511QS:
 *      +---+
 *      | O |   (looking at the front)
 *      |   |
 *       TTT
 *       |||    pin 1 is Vout  ('o' to the left)
 *       123    pin 2 is GND   ('g')
 *       ogV    pin 3 is Vcc   ('V')
 *
 * Warning!  the Sharp GP1UD261XK0F has Vcc and GND swapped!
 *
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include "common.h"
#include "timer.h"
#include "util.h"

#define PULSE_DEBUG 1

/*
 * GPIO usage.  the input needs to come from a pin with a timer
 * "input compare" function.
 */
#define IR_PORT         PORTA
#define IR_PIN          PINA
#define IR_BIT          PA4   // input:  from IR receiver

/* hardware access macro */
#define IR_high()       (IR_PIN & bit(IR_BIT))

// prescaler values for TCCR0B
#define CLKDIV_1    1
#define CLKDIV_8    2
#define CLKDIV_64   3
#define CLKDIV_256  4
#define CLKDIV_1024 5

volatile word capture_len;
volatile byte capture_is_low;
volatile byte capture_overflow;
volatile byte max_pulses;
volatile byte use_low;

#define MAX_PULSES 32
byte ir_i;
long ir_accum, ir_code;
char ir_code_avail;

#if PULSE_DEBUG
word ir_pulse[MAX_PULSES];
#endif


#define usec_per_tick 1

/*
 * set up initial chip conditions
 */
void
ir_init(void)
{
    // enable pullup on IR receiver
    IR_PORT |= bit(IR_BIT);

    // input capture enable, 16 bit mode, and noise canceller
    TCCR0A = bit(ICEN0)|bit(TCW0); // |bit(ICNC0);

    // run the timer at 1usec/tick
#if F_CPU == 1000000
    TCCR0B = CLKDIV_1;
#elif F_CPU == 8000000
    TCCR0B = CLKDIV_8;
#endif

    // start the timer
    TCNT0H = 0;
    TCNT0L = 0;

    // timer0 overflow int enable, and input capture event int enable.
    TIFR = bit(TOV0) | bit(ICF0);   // clear first
    TIMSK |= bit(TOIE0) | bit(TICIE0);
}


/*
 * timer0 overflow interrupt handler.
 * if we hit the overflow without getting a transition on the IR
 * line, then we're certainly "between" IR packets.
 */
ISR(TIMER0_OVF_vect)
{
    capture_overflow = 1;
}

/*
 * input capture event handler
 * the "event" is a transition on the IR line.  we save the
 * captured count, and restart the timer from zero again.
 */
ISR(TIMER0_CAPT_vect, ISR_NOBLOCK)
{
    // save the captured time interval
    capture_len = OCR0A | (OCR0B << 8); // aka ICR0

    // if we captured a rising edge, the pulse was low
    capture_is_low = !!(TCCR0A & bit(ICES0));

    // restart the timer
    TCNT0H = 0;
    TCNT0L = 0;

    // change detection edge, and clear interrupt flag -- it's
    // set as result of detection edge change
    cli();
    TCCR0A ^= bit(ICES0);
    TIFR = bit(ICF0);
    sei();

}


void
ir_process(void)
{
    word len = 0;
    static word lastlen;
    byte low;
    byte overflow;

    /* the "input capture" interrupt handler will record the
     * most recent pulse's length and polarity.
     */
    while (capture_len) {

        cli();
        // capture length, polarity, and overflow data with interrupts off
        len = capture_len;
        low = capture_is_low;
        overflow = capture_overflow;

        capture_overflow = 0;

        // if this is non-zero at top of loop, then a new pulse arrived.
        capture_len = 0;

        sei();

        len *= usec_per_tick;  // ticks -> microseconds

        // led_flash();

        // if we had an overflow or a very long pulse, then the
        // current capture_len is meaningless -- it's just a
        // gap, or the last remnant of a gap.
        if (overflow || len > 10000) {
            ir_i = 0;
            ir_accum = 0;
            lastlen = 0;
            continue;
        }

#define near(val, ref) ((ref + ref/6) > val && val > (ref - ref/6))

        if ( !low &&
            ((near(lastlen, 4850) && near(len, 4550)) ||  // samsung
             (near(lastlen, 2650) && near(len, 500))) )  // sony)
        {
            // putch('H');
            if (1000 > len) {
                use_low = 1;
                max_pulses = 12;
            } else {
                use_low = 0;
                max_pulses = 32;
            }
            ir_i = 0;
            ir_accum = 0;
            lastlen = 0;
            continue;
        }
        lastlen = len;

        if (use_low ^ low) {
            // i don't care about half the pulses.  for my chosen
            // remotes, there's no information in them -- either
            // the low pulses or high pulses are just spacers.
            continue;
        }

        if (ir_i >= max_pulses) { // we've gotten too many bits
            continue;
        }

        // make way for a new bit
        ir_accum <<= 1;

        // zero bits are short, one bits are long
        if (len > 1000)  { // longer than 1 millisecond?
            // putch('1');
            ir_accum |= 1;
        } else {
            // putch('0');
        }

#if PULSE_DEBUG
        ir_pulse[ir_i] = len;
#endif

        // if we've accumulated a full complement of bits, save it off
        if (++ir_i >= max_pulses) {
            ir_code = ir_accum;
            ir_code_avail = 1;
        }
    }
}

#define Nbut 4
long ir_remote_codes[] PROGMEM = {
    // pgf's X-10 "tv buddy" remote (as currently programed)
    0xe0e048b7,     // up (P+)          [IR_UP   ]
    0xe0e008f7,     // down (P-)        [IR_DOWN ]
    0xe0e0f00f,     // center (mute)    [IR_STOP ]
    0xe0e040bf,     // power            [IR_ALT  ]
    // 0xe0e0d02f,     // left (V-)
    // 0xe0e0e01f,     // right (V+)

    // samsung tv remote
    0xe0e006f9 ,     // up               [IR_UP   ]
    0xe0e08679 ,     // down             [IR_DOWN ]
    0xe0e016e9 ,     // enter            [IR_STOP ]
    0xe0e0b44b ,     // exit             [IR_ALT  ]
    // 0xe0e0a659 ,     // left
    // 0xe0e046b9 ,     // right

    // sony video8
    0xd9c,           // rew              [IR_UP   ]
    0x19c,           // stop             [IR_DOWN ]
    0x59c,           // play             [IR_STOP ]
    0x5bc,           // data             [IR_ALT  ]
    // 0x39c,           // ff
    // 0x99c,           // pause
    // 0xc5c,           // slow
    0

};

/*
 * wait for an IR press, and return an index into the table above.
 * this really blocks, so ir_avail() should be called first if that's
 * not desireable.
 *
 * may return -1 if:
 *  - the received code isn't recognized (isn't in table, above)
 *  - the received code matches the previous, and not enough time
 *    has passed.
 */
char get_ir(void)
{
    long *ircp;
    long irc;
    int ir;

    while (1) {
        if (ir_code_avail) {
            static long dup_timer;
            static long last_ir_code;

            ir_code_avail = 0;

            if (!check_timer(dup_timer, 130) && last_ir_code == ir_code) {
                dup_timer = get_ms_timer();
                return -1;
            }

            dup_timer = get_ms_timer();
            last_ir_code = ir_code;

            ircp = ir_remote_codes;

            // loop through the table, and return the index on a match.
            while(1) {
                irc = pgm_read_dword(ircp);
                if (!irc) {
                    p_hex32(ir_code); crnl();
                    return -1;
                }

                if (ir_code == irc) {
                    ir = (ircp - ir_remote_codes) % Nbut;
                    p_hex(ir); crnl();
                    return ir;
                }

                ircp++;
            }
        }
        wdt_reset();
    }
}

void ir_show_code(void)
{
#if PULSE_DEBUG
    byte i;
    for (i = 0; i < MAX_PULSES; i++) {
        puthex(i);
        putstr("\t");
        putdec16(ir_pulse[i]);
        crnl();
    }
#endif

    p_hex32(ir_code);
    crnl();
}

// vile:noti:sw=4
