/*
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
 *
 * Copyright 2002 Karl Bongers (karl@turbobit.com)
 * Copyright 2007,2013 Paul Fox (pgf@foxharp.boston.ma.us)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

/*
 * GPIO usage
 */
#define IRREC_BITNUM	P?   // input:  from IR receiver

/* hardware access macros */
#define IR_high()	(PIND & bit(IRREC_BITNUM))

// values for TCCR1B
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
volatile byte pulse_is_high;
volatile byte had_overflow;

static prog_char version_s[] = AVRLIRC_VERSION "$Revision: 1.52 $";

volatile byte mcusr_mirror;

/*
 * set up initial chip conditions
 */
void
hw_init(void)
{

    mcusr_mirror = MCUSR;
    MCUSR = 0;
    wdt_disable();

    // eliminate div-by-8 (no-op if 'div by 8' clock fuse not programmed)
    CLKPR = bit(CLKPCE);
    CLKPR = 0;


    // setup outputs

    // port D -- just leds are outputs
    DDRD |= bit(LED1_BITNUM);
    DDRD |= bit(LED2_BITNUM);
    // turn off leds
    PORTD |= bit(LED1_BITNUM);
    PORTD |= bit(LED2_BITNUM);
    // enable pullup on IR recvr, and on the debug-mode pin
    PORTD |= bit(IRREC_BITNUM);

    // disable analog comparator -- saves power
    ACSR = bit(ACD);

    // (TCCR1A = 0;)				// default poweron value
    TCCR1B = bit(ICNC1) | CLKDIV_256;	// see comments at emit_pulse_data()
    // timer1 overflow int enable, and input capture event int enable.
    TIMSK = bit(TOIE1) | bit(OCIE1A) | bit(ICIE1);

    // we use the output compare interrupt to turn off the
    // "activity" LED.  this value is around 1/20th of a
    // second for all the "interesting" clock rates (see comments
    // at emit_pulse_data(), below
    OCR1A = 3000;
}


/*
 * timer1 overflow interrupt handler.
 * if we hit the overflow without getting a transition on the IR
 * line, then we're certainly "between" IR packets.  we save the
 * overflow indication until just before the next "real" pulse --
 * lirc wants to see it then (just before the "real" data),
 * rather than at the end.
 */
INTERRUPTIBLE_ISR(TIMER1_OVF_vect)
{
    byte tmp;
    if (IR_high())
	tmp = 0xff;  // high byte of eventual dummy pulselen
    else
	tmp = 0x7f;
    had_overflow = tmp;
}

/*
 * timer1 compare match interrupt handler.  this is simply a way
 * of turning off the "IR message received" LED sooner. 
 * otherwise we could do it in the overflow handler.  this has no
 * affect on the timing protocol, but doing it here makes the LED
 * behavior match the user's button presses a little more
 * closely.
 */
INTERRUPTIBLE_ISR(TIMER1_COMPA_vect)
{
    Led1_Off();
}

/*
 * input capture event handler
 * the "event" is a transition on the IR line.  we save the
 * captured count, and restart the timer from zero again.
 */
INTERRUPTIBLE_ISR(TIMER1_CAPT_vect)
{

    // read the event
    pulse_length = ICR1;
    // and save the new state of the IR line.
    pulse_is_high = IR_high();

    // restart the timer
    TCNT1 = 0;


    // change detection edge, and clear interrupt flag -- it's
    // set as result of detection edge change
    cli();
    TCCR1B ^= bit(ICES1);
    TIFR &= ~bit(ICF1);
    sei();

}

/*
 * pc_int - pin change interrupt.
 * this is purely and simply an inversion function -- we want to
 * present an inverted copy of the USART's TX output to the host's
 * serial port.  so we watch for changes on one pin, and update the other.
 * since this operation needs minimal latency, we reenable interrupts
 * wherever possible in the other interrupt handlers.
 */
ISR(PCINT_vect)
{
    if (PINB & bit(TX_INVERT_IN))
	PORTB &= ~bit(TX_INVERT_OUT);
    else
	PORTB |= bit(TX_INVERT_OUT);
}



/*
 *  we want the timer overflow to be (a lot) longer than the
 *  longest interval we need to record using ICR1, which is
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

void
emit_pulse_data(void)
{
    word len;
    byte high;
    byte overflow;

    while (pulse_length) {
	cli();
	len = pulse_length;
	high = pulse_is_high;
	overflow = had_overflow;

	pulse_length = had_overflow = 0;

	sei();

	Led1_On();
	if (overflow) {
	    // if we had an overflow, then the current pulse_length
	    // is meaningless -- it's just the last remnant of a
	    // long gap.  just send the previously recorded
	    // overflow value to indicate that gap.  this is
	    // effectively the start of a "packet".
	    tx_word((overflow << 8) | 0xff);
	} else {
	    uint32_t l;

	    /* do long arithmetic.  expensive, but we have time. */

	    l = (uint32_t)len * 4096 / scale_denom(FOSC);

	    if (l > 0x7fff)	// limit range.
		len = 0x7fff;
	    else
		len = l;

	    if (len == 0)	// pulse length never zero.
		len++;

	    if (!high)	// report the state we transitioned out of
		len |= 0x8000;

	    tx_word(len);
	}
    }
}

/*
 * main -
 */
int
main(void)
{
    hw_init();
    blinky();

    wdt_enable(WDTO_4S);

    sei();

    for(;;) {
	wdt_reset();
	cli();
	if (!pulse_length) {
	    // only sleep if there's no pulse data to emit
	    // (see <sleep.h> for explanation of this snippet)
	    sleep_enable();
	    sei();
	    sleep_cpu();
	    sleep_disable();
	}
	sei();
	emit_pulse_data();
    }
    /* not reached */

}


