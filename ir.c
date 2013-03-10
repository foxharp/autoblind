/*
 * Copyright 2002 Karl Bongers (karl@turbobit.com)
 * Copyright 2007,2013 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file
 * for details.
 *
 * -----------
 *
 * the IR receiver should be something like the
 * Panasonic PNA4602M (5V),
 * Sharp GP1UD261XK0F (2.7 - 5.5V), or
 * Sharp GP1UX511QS (5V).
 *
 * refer to the datasheet for you specific
 * part.  connect a 10uf to 50uf cap between Vcc and gnd, very
 * close to the detector.
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
 *   (Warning!  the Sharp GP1UD261XK0F has Vcc and GND swapped!)
 *
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include "common.h"
#include "util.h"

/*
 * GPIO usage
 */
#define IRREC_BITNUM	PA4   // input:  from IR receiver
#define IR_PORT		PORTA
#define IR_PIN		PINA

/* hardware access macros */
#define IR_high()	(IR_PIN & bit(IRREC_BITNUM))

// values for TCCR0B
#define CLKDIV_8    2
#define CLKDIV_64   3
#define CLKDIV_256  4
#define CLKDIV_1024 5

/* alternate version of ISR() macro, which doesn't disable all
 * interrupts.
 */
#define INTERRUPTIBLE_ISR(vector)  \
    void vector (void) __attribute__((interrupt)); \
    void vector (void)

volatile word pulse_length;
volatile byte pulse_is_low;
volatile byte had_overflow;

#define MAX_PULSES 16
word ir_pulse[MAX_PULSES + 1];  // there's a header on the front
word ir_code;
byte ir_i;



/*
 * set up initial chip conditions
 */
void
hw_init(void)
{

    // enable pullup on IR recvr, and on the debug-mode pin
    IR_PORT |= bit(IRREC_BITNUM);

    // input capture enable, 16 bit mode, and noise canceller
    TCCR0A = bit(ICEN0)|bit(TCW0)|bit(ICNC0);
    // see comments at emit_pulse_data()
    TCCR0B = CLKDIV_256;

    // timer0 overflow int enable, and input capture event int enable.
    TIMSK |= bit(TOIE0) | bit(TICIE0);
}


/*
 * timer0 overflow interrupt handler.
 * if we hit the overflow without getting a transition on the IR
 * line, then we're certainly "between" IR packets.  we save the
 * overflow indication until just before the next "real" pulse --
 * lirc wants to see it then (just before the "real" data),
 * rather than at the end.
 */
INTERRUPTIBLE_ISR(TIMER0_OVF_vect)
{
    byte tmp;
    if (IR_high())
	tmp = 0xff;  // high byte of eventual dummy pulselen
    else
	tmp = 0x7f;
    had_overflow = tmp;
}

/*
 * input capture event handler
 * the "event" is a transition on the IR line.  we save the
 * captured count, and restart the timer from zero again.
 */
INTERRUPTIBLE_ISR(TIMER0_CAPT_vect)
{

    // read the event
    pulse_length = OCR0A | (OCR0B << 8); // ICR0;
    // and save the new state of the IR line.
    pulse_is_low = IR_high();  // high now means it was a low pulse

    // restart the timer
    TCNT0H = 0;
    TCNT0L = 0;


    // change detection edge, and clear interrupt flag -- it's
    // set as result of detection edge change
    cli();
    TCCR0A ^= bit(ICES0);
    TIFR &= ~bit(ICF0);
    sei();

}


/*
 *  we want the timer overflow to be (a lot) longer than the
 *  longest interval we need to record using ICR0, which is
 *  something like .25 sec.  we also need to convert from
 *  timer count intervals to 16384'ths of a second.
 *     
 *  14.7456Mhz
 *     14745600 counts/sec, prescaled by 256, gives 57600 counts/sec,
 *     or 17.36usec/count, times 65536 gives overflow at 1.14sec.  good.
 *     want 16384'ths:  scale count by 16384 / 57600. ( 4096 / 14400 ) 
 *
 *  12.0000Mhz
 *     12000000 counts/sec, prescaled by 256, gives 46875 counts/sec,
 *     or 21.33usec/count, times 65536 gives overflow at 1.40sec.  good.
 *     want 16384'ths:  scale count by 16384 / 46875. ( 4096 / 11719 )
 *
 *  11.0592
 *     11059200 counts/sec, prescaled by 256, gives 43200 counts/sec,
 *     or 23.15usec/count, times 65536 gives overflow at 1.51sec.  good.
 *     want 16384'ths:  scale count by 16384 / 43200. ( 4096 / 10800 )
 *
 *  8.0000Mhz
 *     8000000 counts/sec, prescaled by 256, gives 31250 counts/sec,
 *     or 32usec/count, times 65536 gives overflow at 2.09sec.  good.
 *     want 16384'ths:  scale count by 16384 / 31250. ( 4096 / 7812 )
 *
 *  3.6864Mhz
 *     3686400/256 --> 14400, so scale by 16384 / 14400 --> 4096 / 3600
 *
 */

#define scale_denom(fosc) ((fosc / 256) / 4)

#define pulse_ms(ms) ((16384 * ms) / 1000) // convert ms to 16384'ths

void
ir_process(void)
{
    word len;
    byte low;
    byte overflow;

    while (pulse_length) {
	cli();
	len = pulse_length;
	low = pulse_is_low;
	overflow = had_overflow;

	pulse_length = had_overflow = 0;

	sei();

	led_flash();
	if (overflow) {
	    // if we had an overflow, then the current pulse_length
	    // is meaningless -- it's just the last remnant of a
	    // long gap.  just send the previously recorded
	    // overflow value to indicate that gap.  this is
	    // effectively the start of a "packet".
	    // tx_word((overflow << 8) | 0xff);
	    ir_i = 0;
	    ir_code = 0;
	} else if (ir_i > MAX_PULSES) {
	    // we've gotten too many bits
	    continue;
	} else if (low) {
	    // i don't care about the low pulses right now.
	    // for my chosen remote, there's no information in
	    // them -- the low pulses are just spacers.
	    continue;
	} else {
	    uint32_t l;

	    /* do long arithmetic.  expensive, but we have time. */

	    l = (uint32_t)len * 4096 / scale_denom(F_CPU);

	    if (l > 0x7fff)	// limit range.
		len = 0x7fff;
	    else
		len = l;

	    if (len == 0)	// pulse length is never zero.
		len++;

	    ir_code <<= 1;
	    if (ir_i >= 2) {
		if (len > pulse_ms(1))
		    ir_code |= 1;
	    }

	    ir_pulse[ir_i++] = len;
	}
    }
}

void ir_show_code(void)
{
    byte i;

    p_hex(ir_code);

    for (i = 0; i < MAX_PULSES; i++) {
	putdec16(i); putstr("  ");
	p_dec(ir_pulse[i]);
    }
}
