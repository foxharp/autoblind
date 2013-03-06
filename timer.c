/* vi: set sw=4 ts=4: */
/*
 * Copyright (c) 2011 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file for details.
 * for details.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#include "timer.h"
#include "util.h"
#include "common.h"

#ifndef NO_MSTIMER

time_t milliseconds;

#define PRINT_TSTAMPS 1
#if PRINT_TSTAMPS
void print_tstamp(void)
{
        unsigned long tval;

        do
        {
                tval = (unsigned long)milliseconds;
        }
        while (tval != (unsigned long)milliseconds);

        // printf("%lu:",tval);
	puthex32(tval); putch(':');

}
#else
void print_tstamp(void) {}
#endif

void init_timer(void)
{
	// set up for simple overflow operation
	TCCR1A = 0;		// normal

	// the counter runs at a microsecond per tick
#if F_CPU == 1000000
	TCCR1B = bit(CS10);   // prescaler is 1
#elif F_CPU == 8000000
	TCCR1B = bit(CS12);   // prescaler is 8
#endif
	// 1000 usec per msec
	TC1H = (1000 >> 8) & 0xff;
	OCR1D = 1000 & 0xff;

	TIMSK |= bit(OCIE1D);

}

ISR(TIMER1_COMPD_vect)
{
	milliseconds++;

	if (milliseconds % 1000 == 0) {
		led_flash();
	}

	timer10bit_add(OCR1D, 1000);
}

time_t get_ms_timer(void)
{
	time_t ms;
	char sreg;

	sreg = SREG;
	cli();

	ms = milliseconds;

	SREG = sreg;

	return ms;
}

unsigned char check_timer(time_t base, int duration)
{
	return get_ms_timer() > (base + duration);
}

void short_delay(unsigned int n)
{
        unsigned int t;
	volatile char sreg;
        for (t=0; t<n; t++)
        {
		sreg = MCUSR;
        }
}

#endif
