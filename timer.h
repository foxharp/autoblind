/*
 * Copyright (C) 2010 One Laptop per Child
 * Copyright (c) 2013 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file
 * for details.
 */


void init_timer(void);
long get_ms_timer(void);
unsigned char check_timer(long time0, int duration);
void print_tstamp(void);

#if F_CPU == 8000000
#define usecs_per_100_loops 0x89   // at 8Mhz
#endif

void short_delay(unsigned int n);  // try not to call this.  use usec_delay()

#define usecs_to_loops(u) ((100*(long)(u))/usecs_per_100_loops)
#define usec_delay(usecs) short_delay(usecs_to_loops(usecs)+1)

#define t1write10(reg, val) {		\
	char sreg = SREG;		\
	cli();				\
					\
	w10tmp = val;			\
	TC1H = (w10tmp >> 8);		\
	reg = w10tmp & 0xff;		\
					\
	SREG = sreg;			\
    }

static inline int t1read10_TCNT1(reg)	\
{					\
	int r10tmp;			\
	int sreg = SREG;		\
	cli();				\
					\
	r10tmp = TCNT1 & 0xff;		\
	r10tmp |= TC1H << 8;		\
					\
	SREG = sreg;			\
	return r10tmp;			\
}

#if RX_USE_INPUT_CAPTURE_INT
static inline int t1read10_ICR1(reg)	\
{					\
	int r10tmp;			\
	int sreg = SREG;		\
	cli();				\
					\
	r10tmp = ICR1 & 0xff;		\
	r10tmp |= ICR1 << 8;		\
					\
	SREG = sreg;			\
	return r10tmp;			\
}
#endif

#define t1add10(reg, incr) {		\
	unsigned int next;		\
					\
	char sreg = SREG;		\
	cli();				\
					\
	next = reg;			\
	next |= TC1H << 8;		\
	next += (incr);			\
	TC1H = next >> 8;		\
	reg = next & 0xff;		\
					\
	SREG = sreg;			\
    }


/* vi: set sw=4 ts=4: */
