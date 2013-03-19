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
#include "common.h"
#include "util.h"

#define PULSE_DEBUG 1

/*
 * GPIO usage.  the input needs to come from a pin with a timer
 * "input compare" function.
 */
#define IR_BIT		PA4   // input:  from IR receiver
#define IR_PORT		PORTA
#define IR_PIN		PINA

/* hardware access macro */
#define IR_high()	(IR_PIN & bit(IR_BIT))

// prescaler values for TCCR0B
#define CLKDIV_8    2
#define CLKDIV_64   3
#define CLKDIV_256  4
#define CLKDIV_1024 5

volatile word pulse_length;
volatile byte pulse_is_low;
volatile byte had_overflow;

#define MAX_PULSES 32
#ifdef PULSE_DEBUG
word ir_pulse[MAX_PULSES];  // there's a header on the front
#endif
long ir_accum, ir_code;
byte ir_i;


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
 *  so for (almost) all clock rates, prescaling by 256 seems right.
 *
 */

#define usec_per_tick (256 * 1000000 / F_CPU)

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

    TCCR0B = CLKDIV_256;	// see comments above

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
    had_overflow = 1;
}

/*
 * input capture event handler
 * the "event" is a transition on the IR line.  we save the
 * captured count, and restart the timer from zero again.
 */
ISR(TIMER0_CAPT_vect, ISR_NOBLOCK)
{
    // save the captured time interval
    pulse_length = OCR0A | (OCR0B << 8); // aka ICR0

    // if we captured a rising edge, the pulse was low
    pulse_is_low = (TCCR0A & bit(ICES0));

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

    while (pulse_length) {

	cli();
	// capture length, polarity, and overflow data with interrupts off
	len = pulse_length;
	low = pulse_is_low;
	overflow = had_overflow;

	had_overflow = 0;
	pulse_length = 0;

	sei();

	len *= usec_per_tick;  // ticks -> usec

	// led_flash();
	if (overflow || len > 10000) {
	    // if we had an overflow, then the current pulse_length
	    // is meaningless -- it's just the last remnant of a
	    // long gap.
	    ir_i = 0;
	    ir_accum = 0;
	    lastlen = 0;
	    continue;
	} 
	
	// there's a header of low/high of about 4500usec each
	if (!low &&
	    5000 > len     && len > 4000 &&
	    5000 > lastlen && lastlen > 4000) {
	    // it's a header
	    ir_i = 0;
	    ir_accum = 0;
	    lastlen = 0;
	    continue;
	}
	lastlen = len;
	
	if (low) {
	    // i don't care about the low pulses right now.
	    // for my chosen remote, there's no information in
	    // them -- the low pulses are just spacers.
	    continue;
	} 

	if (ir_i >= MAX_PULSES) { // we've gotten too many bits
	    continue;
	}

	ir_accum <<= 1;
	if (len > 1000)  // longer than 1 millisecond?
	    ir_accum |= 1;

#ifdef PULSE_DEBUG
	ir_pulse[ir_i++] = len;
#endif

	if (ir_i >= MAX_PULSES)
	    ir_code = ir_accum;
    }
}

void ir_show_code(void)
{
#ifdef PULSE_DEBUG
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
