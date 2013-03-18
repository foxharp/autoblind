/* vi: set sw=4 ts=4: */
/*
 * Copyright (c) 2012 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file
 * for details.
 *
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include "suart.h"
#include "timer.h"
#include "util.h"
#include "common.h"

#ifdef USE_PRINTF
#include <stdio.h>

static int stdio_putchar(char c, FILE *stream);

static FILE mystdout = FDEV_SETUP_STREAM(stdio_putchar, NULL, _FDEV_SETUP_WRITE);

static int
stdio_putchar(char c, FILE *stream)
{
	putch(c);
	return 0;
}
#endif

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
	putch('0' + (i%10));
}

#if ! ALL_STRINGS_PROGMEM
void putstr(const prog_char * s)	// send string
{
	while (*s)
		putch(*s++);
}
#endif

void putstr_p(const prog_char * s)
{
	char c;
	while ((c = pgm_read_byte(s++)))
		putch(c);
}


# define DDRLED DDRB
# define PORTLED PORTB
# define PINLED PINB
# define BITLED PB0

#define Led1_On()	do { PORTLED |=  bit(BITLED); } while(0)
#define Led1_Off()	do { PORTLED &= ~bit(BITLED); } while(0)
#define Led1_is_On()	   ( PINLED  &   bit(BITLED) )

static long led_time;

void init_led(void)
{
	DDRLED |= bit(BITLED);
}

void led_handle(void)
{
	if (led_time && Led1_is_On() && check_timer(led_time, 100)) {
		Led1_Off();
		led_time = 0;
	}
}

void led_flash(void)
{
	Led1_On();
	led_time = get_ms_timer();
	return;
}

/*
 * delay - wait a bit
 */
void
delay(word dly)
{
    volatile word i;
    volatile byte j;

    for (i = dly; i != 0; i--)
	for (j = 255; j != 0; j--)
	    /* nothing */;
}

/*
 * wiggling light pattern, to show life at startup.
 * useful for visually detecting watchdog or crash.
 */
void
blinky(void)
{
    byte i;
    for (i = 0; i < 12; i++) {
	if (i & 1) {
	    Led1_Off();
	} else {
	    Led1_On();
	}
	delay(50);
    }
}

void
do_debug_out(void)
{
    if (do_fox()) {
	/* a perfect square wave is useful for debugging baud rate issues
	 */
	for(;;) {
	    putch('U');
	}
	/* not reached */
    }
}


void
util_init(void)
{
#ifdef USE_PRINTF
	stdout = &mystdout;
#endif
}

