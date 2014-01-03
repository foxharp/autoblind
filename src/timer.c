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
#include "blind.h"
#include "limits.h"
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

    // arrange to hit wraparound soon (5 minutes)
    milliseconds = LONG_MAX - (5L * 60L * 1000L);

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
        blind_report();
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

void set_ms_timer(long ms)
{
    char sreg;

    sreg = SREG;
    cli();

    milliseconds = ms;

    SREG = sreg;
}

unsigned char check_timer(long t0, long delta)
{
    // the timer will eventually wrap from positive (0x7fffffff)
    // to negative (0x80000000).  using a signed type for the
    // timer and expressing the arithmetic carefully will
    // guarantee that interval timers work correctly across this
    // transition.  (at least as long as the interval timers are
    // shorter than 2^31.)

    return (get_ms_timer() - t0 > delta);
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
