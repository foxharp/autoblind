/*
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
#include "ir.h"
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

volatile static word capture_len;
volatile static byte capture_is_low;
volatile static byte capture_overflow;

#define MAX_PULSES 48
static byte ir_i;
static long ir_accum, ir_code;
static char ir_code_avail;

#if PULSE_DEBUG
static struct pulsepair {
    unsigned int lowlen;
    unsigned int highlen;
} ir_header, ir_times[MAX_PULSES];
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
    static word lowlen;
    byte low;
    byte overflow;

    /* the "input capture" interrupt handler will record the
     * most recent pulse's length and polarity.
     */
    while (capture_len || capture_overflow) {

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
            if (ir_i > 3) {
                ir_code = ir_accum;
                ir_code_avail = 1;
            }
            ir_i = 0;
            ir_accum = 0;
            lowlen = 0;
            // overflow ? putch('v'):putch('o');
            overflow = 0;
            continue;
        }


        if ( !low &&
#if BEFORE
#define near(val, ref) ((ref + ref/6) > val && val > (ref - ref/6))
            /* remotes emit different header pairs... */
            ((near(lowlen, 4850) && near(len, 4550)) ||  // samsung
             (near(lowlen, 2650) && near(len, 500))  ||  // sony
             (near(lowlen, 3750) && near(len, 1670)) ||  // panasonic
             (near(lowlen, 9065) && near(len, 4500)))    // packard bell
#else
            /* ... but they have a lot in common */
            (len > 2000 || lowlen > 2000)
#endif
            )
        {
            ir_i = 0;
#if PULSE_DEBUG
            ir_header.lowlen = lowlen;
            ir_header.highlen = len;
#endif
            ir_accum = 0;
            lowlen = 0;
            continue;
        }

        if (low) {
            /* we don't bother recording low pulses.  just keep
             * track of how long they were. */
            lowlen = len;
            continue;
        }

        // if a remote puts out lots of bits (e.g., panasonic),
        // we'll only save the last 32 of them.  by setting
        // MAX_PULSES on the high side (i.e., 48, rather than
        // just 32), we're hoping that leading bits are likely a
        // fixed prefix that we can discard without losing
        // uniqueness.
        if (ir_i >= MAX_PULSES) { // we've gotten too many bits
            lowlen = len;
            continue;
        }

        // make way for a new bit
        ir_accum <<= 1;

        // zeros and ones may be short or long.  we don't really
        // care.  what's important is that in all remotes i've
        // looked at, either the low pulse is always short, or
        // the high pulse is always short -- whichever it is, the
        // bit is descriminated by the other one being longer
        // than a millisecond. */
        if (len > 1000 || lowlen > 1000)  {
            ir_accum |= 1;
        }

#if PULSE_DEBUG
        ir_times[ir_i].lowlen = lowlen;
        ir_times[ir_i].highlen = len;
#endif

        lowlen = 0;

        // if we've accumulated a full complement of bits, save it off
        if (++ir_i >= MAX_PULSES) {
            ir_code = ir_accum;
            ir_code_avail = 1;
        }
    }
}

/* to add a remote, press the keys you've chosen for all of
 * up/down/middle/stop/alt and copy/paste them to this table.
 */
struct irc {
    long ir_code;
    char ir_cmd;
} ir_remote_codes [] PROGMEM = {
    // pgf's X-10 "tv buddy" remote (as currently programed)
    { 0xe0e048b7, IR_TOP },        // up (P+)
    { 0xe0e0d02f, IR_MIDDLE },     // left (V-)
    { 0xe0e0e01f, IR_MIDDLE },     // right (V+)
    { 0xe0e008f7, IR_BOTTOM },     // down (P-)
    { 0xe0e0f00f, IR_STOP },       // center (mute)
    { 0xe0e040bf, IR_ALT },        // power

    // samsung tv remote
    { 0xe0e006f9, IR_TOP },        // up
    { 0xe0e0a659, IR_MIDDLE },     // left
    { 0xe0e046b9, IR_MIDDLE },     // right
    { 0xe0e08679, IR_BOTTOM },     // down
    { 0xe0e016e9, IR_STOP },       // enter
    { 0xe0e0b44b, IR_ALT },        // exit

    // ancient packard bell 
    { 0x08f7906f, IR_TOP },        // up     
    { 0x08f710ef, IR_MIDDLE },     // left   
    { 0x08f7d02f, IR_MIDDLE },     // right  
    { 0x08f750af, IR_BOTTOM },     // down   
    { 0x08f7708f, IR_STOP },       // enter  
    { 0x08f7e21d, IR_ALT },        // aux3   

    // sony video8
    { 0x2de, IR_ALT },             // data
    { 0x6ce, IR_TOP },             // rew
    { 0x1ce, IR_TOP },             // ff
    { 0x2ce, IR_MIDDLE },          // play
    { 0x0ce, IR_STOP },            // stop
    { 0x4ce, IR_BOTTOM },          // pause
    { 0x62e, IR_BOTTOM },          // slow

    // panasonic dvd
    { 0x0d00a1ac, IR_TOP },        // up     
    { 0x0d00e1ec, IR_MIDDLE },     // left   
    { 0x0d00111c, IR_MIDDLE },     // right  
    { 0x0d00616c, IR_BOTTOM },     // down   
    { 0x0d00414c, IR_STOP },       // enter  
    { 0x0d00010c, IR_ALT },        // menu   

    { 0, 0}
};

/*
 * if there's an IR press available, return an index into the table above.
 *
 * may return 0 if:
 *  - no press is available
 *  - the received code isn't configured in table, above.
 *  - the received code matches the previous, and not enough time
 *    has passed.
 */
char get_ir(void)
{
    struct irc *ircp;
    long ircode;
    int ircmd;

    static long dup_timer;
    static long last_ir_code;

    if (!ir_code_avail)
        return 0;

    ir_code_avail = 0;

    if (!check_timer(dup_timer, 130) && last_ir_code == ir_code) {
        dup_timer = get_ms_timer();
        return 0;
    }

    dup_timer = get_ms_timer();
    last_ir_code = ir_code;

    ircp = ir_remote_codes;

    // loop through the table, and return the index on a match.
    while(1) {
        ircode = pgm_read_dword(&ircp->ir_code);
        if (!ircode) {
            p_hex32(ir_code); crnl();
            return 0;
        }

        if (ir_code == ircode) {
            ircmd = pgm_read_byte(&ircp->ir_cmd);
            p_hex32(ir_code); 
            p_hex(ircmd); crnl();
            return ircmd;
        }

        ircp++;
    }
}

void ir_show_code(void)
{
#if PULSE_DEBUG
    byte i;

    putstr("head:\t");
    putdec16(ir_header.lowlen);
    putch('\t');
    putdec16(ir_header.highlen);
    crnl();
    for (i = 0; i < MAX_PULSES; i++) {
        puthex(i);
        putch('\t');
        putdec16(ir_times[i].lowlen);
        putch('\t');
        putdec16(ir_times[i].highlen);
        crnl();
    }
#endif

    p_hex32(ir_code);
    crnl();
}

// vile:noti:sw=4
