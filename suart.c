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

// timer running at 1Mhz
#define BIT_TIME    (unsigned int)((1000000 + BAUD/2) / BAUD)

volatile unsigned char stx_bits;
volatile unsigned char stx_data;

#ifndef NO_RECEIVE
volatile unsigned char srx_done;
volatile unsigned char srx_data;
volatile unsigned char srx_mask;
volatile unsigned char srx_tmp;
#endif

// search for "Table 12-8.  Compare Output Mode, Normal Mode
// (non-PWM)" or something similar in the datasheet to see
// what's happening here.  these select the output level that
// will be set on the next timer1 compare match A.
#if TX_INVERT
#define SET_TX_LOW_NEXT (bit(COM1A1)|bit(COM1A0))   // set high
#define SET_TX_HIGH_NEXT (bit(COM1A1))              // set low
#else
#define SET_TX_LOW_NEXT (bit(COM1A1))               // set low
#define SET_TX_HIGH_NEXT (bit(COM1A1)|bit(COM1A0))  // set high
#endif


void suart_init(void)
{
    int w10tmp;

    // timer_init has already configured the basic rate --
    // the 10-bit timer is running at 1Mhz

    STXDDR |= bit(STX);         // set TX as output

    // configure timer to go high on next compare match...
    TCCR1A = SET_TX_HIGH_NEXT;  // set OC1A high, T1 mode 0

    // ...and force that compare to happen soon.
    t1write10(OCR1A, t1read10_TCNT1() + 25);

    stx_bits = 0;               // nothing to send right now
    STIMSK |= bit(OCIE1A);      // enable tx


#ifndef NO_RECEIVE
# if RX_USE_INPUT_CAPTURE_INT
    // enable noise canceller
    TCCR1B = bit(ICNC1);
#  if RX_INVERT
    // look for rising rather than falling start-bit edge
    TCCR1B |= bit(ICES1);
#  endif
    // CLK/1, T1 mode 0
    STIFR = bit(ICF1);      // clear pending interrupt
    STIMSK |= bit(ICIE1);   // enable rx and wait for start
# else
    // configure falling edge on INT0 (NB: and also on INT1)
#  if ! RX_INVERT
    MCUCR = bit(ISC01);
#  endif

    // clear and enable start bit edge interrupt
    GIFR = bit(INTF0);
    GIMSK |= bit(INT0);
# endif

    srx_done = 0;               // nothing received
#endif
}


#ifndef NO_RECEIVE
unsigned char getch(void)       // get byte
{
    while (!srx_done)           // wait until byte received
        /* loop */ ;
    srx_done = 0;
    return srx_data;
}


// SRX_HIGH() -- is the received bit high?
#if RX_INVERT
#define SRX_HIGH(x) (((x) & bit(SRX)) == 0)
#else
#define SRX_HIGH(x) ((x) & bit(SRX)) 
#endif


// we've seen the falling edge of the start bit
#if RX_USE_INPUT_CAPTURE_INT
ISR(TIMER1_CAPT_vect)       // rx start
#else
ISR(INT0_vect)              // rx start
#endif
{
    int w10tmp;

    // schedule our next interrupt 1.5 bits from now
#if RX_USE_INPUT_CAPTURE_INT
    t1write10(OCR1B, t1read10_ICR1() + (unsigned int) (3 * BIT_TIME / 2));
#else
    GIMSK &= ~bit(INT0);
    t1write10(OCR1B, (t1read10_TCNT1() + (unsigned int) (3 * BIT_TIME / 2)));
#endif

    srx_tmp = 0;                // clear incoming byte storage
    srx_mask = 1;               // the bit we're expecting next
    STIFR = bit(OCF1B);         // clear pending interrupt

    if (!SRX_HIGH(SRXPIN)) {    // still low (i.e., start bit)
        STIMSK |= bit(OCIE1B);  // wait for first bit
    }

}


ISR(TIMER1_COMPB_vect)   // time to sample the value of an RX bit
{
    unsigned char in = SRXPIN;  // grab current rx level

    if (srx_mask) {
        // schedule interrupt for next bit sample
        t1add10(OCR1B, BIT_TIME);

        // record the received bit
        if (SRX_HIGH(in))
            srx_tmp |= srx_mask;

        srx_mask <<= 1;

    } else {
        // all done
        srx_done = 1;               // mark rx data valid
        srx_data = srx_tmp;         // store rx data

        // disable the bit sampling interrupt
        STIMSK &= ~bit(OCIE1B);     // disable rx bit timer

        // clear and enable the start-bit edge interrupt
#if RX_USE_INPUT_CAPTURE_INT
        STIFR = bit(ICF1);
        STIMSK |= bit(ICIE1)
#else
        GIFR = bit(INTF0);
        GIMSK |= bit(INT0);
#endif
    }
}
#endif


void putch(char val)        // send a character
{
    if (val == '\n')
        putch('\r');

    while (stx_bits)        // loop until previous character is sent
        /* loop */ ;

    // we need to send 10 bits, but can only store 8.  the
    // start bit is 0, the stop bit is 1.  we special-case the
    // start bit when transmitting, but can get the stop bit
    // for "free" by inverting the data.
    stx_data = ~val;        // invert data for Stop bit generation
    stx_bits = 10;          // 1 start bit + 8 + 1 stop bit
}


ISR(TIMER1_COMPA_vect, ISR_NOBLOCK)  // time to transmit a bit
{
    unsigned char dout;
    unsigned char remaining;


    // schedule another interrupt one bit-time from now
    t1add10(OCR1A, BIT_TIME);

    remaining = stx_bits;

    if (remaining) {
        dout = SET_TX_LOW_NEXT;
        if (remaining != 10) {          // all except for the start bit
            if ((stx_data & 1) == 0)    // test inverted data
                dout = SET_TX_HIGH_NEXT;
            stx_data >>= 1;             // zero fill from left, gives stop bit
        }
        TCCR1A = dout;
        stx_bits = remaining - 1;       // count down
    }
}

// vile:noti:sw=4
