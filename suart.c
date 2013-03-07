/* vi: set sw=4 ts=4: */
/************************************************************************/
/*                                                                      */
/*                      Software UART using T1                          */
/*                                                                      */
/*              Author: P. Dannegger                                    */
/*                      danni@specs.de                                  */
/*                                                                      */
/************************************************************************/
/*
 * This file included in irmetermon by written permission of the
 * author.  irmetermon is licensed under GPL version 2, see accompanying
 * LICENSE file for details.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include "common.h"
#include "timer.h"
#include "suart.h"

#define TX_INVERT 0
#define RX_INVERT 0

#define bit(x) _BV(x)

#define BIT_TIME	(unsigned int)((F_CPU + BAUD/2) / BAUD)


volatile unsigned char stx_count;
unsigned char stx_data;

#ifndef NO_RECEIVE
volatile unsigned char srx_done;
unsigned char srx_data;
unsigned char srx_mask;
unsigned char srx_tmp;
#endif


void suart_init(void)
{
	// timer_init has already configured the rate

	OCR1A = TCNT1 + 1;			// force first compare
	TCCR1A = bit(COM1A1) | bit(COM1A0);	// set OC1A high, T1 mode 0

	STIMSK |= bit(OCIE1A);			// enable tx
	stx_count = 0;				// nothing to sent
	STXDDR |= bit(STX);			// TX output

#ifndef NO_RECEIVE
# if RX_USE_INPUT_CAPTURE_INT
	// enable noise canceller
	TCCR1B = bit(ICNC1);
#  if RX_INVERT
	// rising rather than falling transition
	TCCR1B |= bit(ICES1);
#  endif
	// CLK/1, T1 mode 0
	STIFR = bit(ICF1);	// clear pending interrupt
	STIMSK |= bit(ICIE1);	// enable rx and wait for start
# else
	MCUCR = bit(ISC01);	// falling edge on INT0 (and INT1)
	GIFR = bit(INTF0);	// clear pending interrupt
	GIMSK = bit(INT0);	// enable rx and wait for start bit
# endif

	srx_done = 0;				// nothing received
#endif
}


#ifndef NO_RECEIVE
unsigned char getch(void)	// get byte
{
	while (!srx_done);			// wait until byte received
	srx_done = 0;
	return srx_data;
}


#if RX_USE_INPUT_CAPTURE_INT
ISR(TIMER1_CAPT_vect)		// rx start
#else
ISR(INT0_vect)		// rx start
#endif
{
	// scan 1.5 bits after start
#if RX_USE_INPUT_CAPTURE_INT
	OCR1B = ICR1 + (unsigned int) (3 * BIT_TIME / 2);
#else
	GIMSK &= ~bit(INT0);
	OCR1B = TCNT1 + (unsigned int) (3 * BIT_TIME / 2);
#endif

	srx_tmp = 0;				// clear bit storage
	srx_mask = 1;				// bit mask
	STIFR = bit(OCF1B);			// clear pending interrupt
#if RX_INVERT
	if (SRXPIN & bit(SRX))		// still high
#else
	if (!(SRXPIN & bit(SRX)))	// still low
#endif
		STIMSK = bit(OCIE1B);	// wait for first bit
}


ISR(TIMER1_COMPB_vect)
{
	unsigned char in = SRXPIN;	// scan rx line

	if (srx_mask) {
		OCR1B += BIT_TIME;		// next bit slice
#if RX_INVERT
		if (!(in & bit(SRX)))
#else
		if (in & bit(SRX))
#endif
			srx_tmp |= srx_mask;
		srx_mask <<= 1;
	} else {
		srx_done = 1;			// mark rx data valid
		srx_data = srx_tmp;		// store rx data
		STIMSK = bit(OCIE1A);		// enable tx
#if RX_USE_INPUT_CAPTURE_INT
		STIFR = bit(ICF1);		// clear pending interrupt
		STIMSK |= bit(ICIE1)		// enable rx
#else
		GIFR = bit(INTF0);		// clear pending interrupt
		GIMSK = bit(INT0);		// enable rx
#endif
	}
}
#endif


void putch(char val)		// send byte
{
	if (val == '\n')
		putch('\r');

	while (stx_count)	// until last byte finished
	    /* loop */ ;

	stx_data = ~val;	// invert data for Stop bit generation
	stx_count = 10;		// 10 bits: Start + data + Stop
}



ISR(TIMER1_COMPA_vect)	// tx bit
{
	unsigned char dout;
	unsigned char count;

	timer10bit_add(OCR1A, BIT_TIME);	// next bit slice

	count = stx_count;

	if (count) {
		stx_count = --count;	// count down
#if TX_INVERT
		dout = bit(COM1A1)|bit(COM1A0);	// set high on next compare
#else
		dout = bit(COM1A1);		// set low on next compare
#endif
		if (count != 9) {		// no start bit
			if (!(stx_data & 1))	// test inverted data
#if TX_INVERT
				dout = bit(COM1A1);	// set low on next compare
#else
				dout = bit(COM1A1)|bit(COM1A0);	// set high on next compare
#endif
			stx_data >>= 1;		// shift zero in from left
		}
		TCCR1A = dout;
	}
}
