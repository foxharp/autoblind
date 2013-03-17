/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2010 One Laptop per Child
 * Copyright (c) 2012 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file
 * for details.
 */

typedef unsigned long time_t;

void init_timer(void);
time_t get_ms_timer(void);
unsigned char check_timer(time_t time0, int duration);
void print_tstamp(void);

#define usecs_per_100_loops 59   // probably wrong

void short_delay(unsigned int n);  // try not to call this.  use usec_delay()

#define usecs_to_loops(u) ((100*(long)(u))/usecs_per_100_loops)
#define usec_delay(usecs) short_delay(usecs_to_loops(usecs)+1)

#define timer10bit_write(reg, val) {	\
	wtmp = val;			\
	TC1H = (wtmp >> 8) & 0x03;	\
	reg = wtmp & 0xff;		\
    }

#define timer10bit_read(reg) (rtmp = (TC1H << 8), rtmp + (reg & 0xff))

#define timer10bit_add(reg, incr) {	\
	unsigned int next;		\
	next = reg;			\
	next |= (TC1H << 8);		\
	next += (incr);			\
	TC1H = (next >> 8) & 0x03;	\
	reg = next & 0xff;		\
    }

