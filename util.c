/* vi: set sw=4 ts=4: */
/*
 * Copyright (c) 2012 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file
 * for details.
 *
 */

#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include "suart.h"
#include "timer.h"
#include "common.h"

static int stdio_putchar(char c, FILE *stream);

static FILE mystdout = FDEV_SETUP_STREAM(stdio_putchar, NULL, _FDEV_SETUP_WRITE);

static int
stdio_putchar(char c, FILE *stream)
{
	putch(c);
	return 0;
}

void
util_init(void)
{
	stdout = &mystdout;
}

void puthex(unsigned char i)
{
	unsigned char j;

	j = i >> 4;
	i &= 0xf;

	if (j >= 10)
		putch(j + 'a' - 10);
	else
		putch(j + '0');
	if (i >= 10)
		putch(i + 'a' - 10);
	else
		putch(i + '0');
}

void puthex16(unsigned int i)
{
	puthex((i >> 8) & 0xff);
	puthex((i >> 0) & 0xff);
}

void puthex32(long l)
{
	puthex((l >> 24) & 0xff);
	puthex((l >> 16) & 0xff);
	puthex((l >> 8) & 0xff);
	puthex((l >> 0) & 0xff);
}

void putdec16(unsigned int i)
{
	if (i > 9)
		putdec16(i/10);
	putchar('0' + (i%10));
}

# define PORTLED PORTB
# define BITLED PB6
# define DDRLED DDRB
time_t led_time;

void init_led(void)
{
	DDRLED |= bit(BITLED);
}

void led_handle(void)
{
	if ((PORTLED & bit(BITLED)) && check_timer(led_time, 100))
		PORTLED &= ~bit(BITLED);
}

void led_flash(void)
{
	PORTLED |= bit(BITLED);
	led_time = get_ms_timer();
}

