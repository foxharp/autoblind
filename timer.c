/*
 * Copyright (C) 2010 One Laptop per Child
 * Copyright (c) 2013 Paul Fox, pgf@foxharp.boston.ma.us
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

long milliseconds;

#define PRINT_TSTAMPS 1
#if PRINT_TSTAMPS
void print_tstamp(void)
{
    long tval;

    do {
        tval = milliseconds;
    } while (tval != milliseconds);

    // printf("%lu:",tval);
    puthex32(tval); putch(':');

}
#else
void print_tstamp(void) {}
#endif

void init_timer(void)
{
    int w10tmp;

    TCCR1A = 0;     // normal port operation

    // the counter runs at a microsecond per tick.
    // this timer is shared by suart.c, so changing
    // the rate here has an affect there as well.
#if F_CPU == 1000000
    TCCR1B = bit(CS10);   // prescaler is 1
#elif F_CPU == 8000000
    TCCR1B = bit(CS12);   // prescaler is 8
#endif
    // TOP value -- all ones
    t1write10(OCR1C, 0x3ff);

    // we want an interrupt every millisecond for timekeeping.
    // use the 'D' comparator get an interrupt every 1000us.
    t1write10(OCR1D, 1000);

    TIMSK |= bit(OCIE1D);
}

ISR(TIMER1_COMPD_vect)
{
    // reprime the comparator for 1ms in the future
    t1add10(OCR1D, 1000);

    milliseconds++;

    sei();

    tone_cycle();

    // approximately 1/second (much cheaper, codewise, than "% 1000")
    if ((milliseconds & 1023) == 0) {
        led_flash();
    }
}

long get_ms_timer(void)
{
    long ms;
    char sreg;

    sreg = SREG;
    cli();

    ms = milliseconds;

    SREG = sreg;

    return ms;
}

unsigned char check_timer(long base, long duration)
{
    return base && (get_ms_timer() > (base + duration));
}

void short_delay(unsigned int n)
{
    unsigned int t;
    volatile char sreg;
    for (t=0; t<n; t++) {
        sreg = MCUSR;
    }
}

#endif

// vile:noti:sw=4
