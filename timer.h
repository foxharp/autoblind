/* vi: set sw=4 ts=4: */
/*
 * Copyright (c) 2012 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file
 * for details.
 */

typedef unsigned long time_t;


void init_timer(void);
time_t get_ms_timer(void);
#define get_ms_timer_ext get_ms_timer
unsigned char check_timer(time_t time0, int duration);
void print_tstamp(void);

#ifdef XOCHARGE_ADAFRUITU4
#define usecs_per_100_loops 59
#else
#ifdef SDCC
#define usecs_per_100_loops 310   // 3.1 usec per loop
#else // Keil
#define usecs_per_100_loops 329
//#define usecs_per_100_loops 174
#endif
#endif

void short_delay(unsigned int n);  // try not to call this.  use usec_delay()

#define usecs_to_loops(u) ((100*(long)(u))/usecs_per_100_loops)
#define usec_delay(usecs) short_delay(usecs_to_loops(usecs)+1)

