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

// timer running at 1Mhz
#define BIT_TIME	(unsigned int)((1000000 + BAUD/2) / BAUD)

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
	int r10tmp,w10tmp;
	// timer_init has already configured the rate

	// OCR1A = TCNT1 + 1;			// force first compare
	t1write10(OCR1A, t1read10(TCNT1) + 1);
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


// check if the received bit is high
#if RX_INVERT
#define SRX_HIGH(x) (((x) & bit(SRX)) == 0)
#else
#define SRX_HIGH(x) ((x) & bit(SRX)) 
#endif


#if RX_USE_INPUT_CAPTURE_INT
ISR(TIMER1_CAPT_vect)		// rx start
#else
ISR(INT0_vect)		// rx start
#endif
{
	int r10tmp, w10tmp;
	// scan 1.5 bits after start
#if RX_USE_INPUT_CAPTURE_INT
	t1write10(OCR1B, t1read10(ICR1) + (unsigned int) (3 * BIT_TIME / 2));
#else
	GIMSK &= ~bit(INT0);
	
	t1write10(OCR1B, (t1read10(TCNT1) + (unsigned int) (3 * BIT_TIME / 2)));
#endif

	srx_tmp = 0;				// clear bit storage
	srx_mask = 1;				// bit mask
	STIFR = bit(OCF1B);			// clear pending interrupt

	if (!SRX_HIGH(SRXPIN))		// still low (i.e., start bit)
		STIMSK = bit(OCIE1B);	// wait for first bit
}


ISR(TIMER1_COMPB_vect)
{
	unsigned char in = SRXPIN;	// scan rx line

	if (srx_mask) {
		t1add10(OCR1B, BIT_TIME);	// next bit slice
		if (SRX_HIGH(in))
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

	usec_delay(1);	

	stx_data = ~val;	// invert data for Stop bit generation
	stx_count = 10;		// 10 bits: Start + data + Stop
}


// search for "Table 12-8.  Compare Output Mode, Normal Mode
// (non-PWM)" or something similar in the datasheet to see
// what's happening here.
#if TX_INVERT
#define SET_TX_LOW_NEXT (bit(COM1A1)|bit(COM1A0))	// set high
#define SET_TX_HIGH_NEXT (bit(COM1A1))			// set low
#else
#define SET_TX_LOW_NEXT (bit(COM1A1))			// set low
#define SET_TX_HIGH_NEXT (bit(COM1A1)|bit(COM1A0))	// set high
#endif

ISR(TIMER1_COMPA_vect)	// tx bit
{
	unsigned char dout;
	unsigned char count;

	t1add10(OCR1A, BIT_TIME);	// next bit slice

	count = stx_count;

	if (count) {
		stx_count = --count;	// count down
		dout = SET_TX_LOW_NEXT;
		if (count != 9) {		// no start bit
			if (!(stx_data & 1))	// test inverted data
				dout = SET_TX_HIGH_NEXT;
			stx_data >>= 1;		// shift zero in from left
		}
		TCCR1A = dout;
	}
}
