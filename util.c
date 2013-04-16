/*
 * Copyright (c) 2013 Paul Fox, pgf@foxharp.boston.ma.us
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

/* set up the plumbing for printf(), so it calls our character
 * output routine */
static int stdio_putchar(char c, FILE *stream);
static FILE mystdout = FDEV_SETUP_STREAM(stdio_putchar, NULL, _FDEV_SETUP_WRITE);
static int
stdio_putchar(char c, FILE *stream)
{
    putch(c);
    return 0;
}
#endif

// print a single byte as hex
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
/* if not all strings are in program memory, then we need
 * different printing routines for each type.  otherwise,
 * we only need a routine that uses pgm_read_byte() to
 * fetch the string.
 */
void putstr(const prog_char * s)    // send string
{
    while (*s)
        putch(*s++);
}

void putstr_p(const prog_char * s)
#else
void putstr(const prog_char * s)
#endif
{
    char c;
    while ((c = pgm_read_byte(s++)))
        putch(c);
}


/*
 * LED
 */
static long led_time;

void init_led(void)
{
    DDRLED |= bit(BITLED);  // set to output
}

/* commence a timed flash */
void led_flash(void)
{
    led1_on();
    led_time = get_ms_timer();
    return;
}

/* turn off a timed LED flash */
void led_handle(void)
{
    if (led1_is_on() && check_timer(led_time, 100))
        led1_off();
}


/*
 * tones
 */
static long tone_time;
static int tone_duration;
char tone_on, tonecnt;

void tone_hw_enable(void)
{
    // we drive the piezo buzzer with two gpio pins, in push-pull mode
    DDRTONE |= TONEBITS;
    // init one bit high, the other low -- both are flipped when we cycle
    PORTTONE |= ONE_TONEBIT;
}
void tone_hw_disable(void)
{
    PORTTONE &= ~TONEBITS;
    DDRTONE &= ~TONEBITS;
}

void tone_start(char hilo, int duration)
{
    tone_on = hilo;
    tone_time = get_ms_timer();
    tone_duration = duration;
    tone_hw_enable();
    return;
}

void tone_handle(void)
{
    /* silence tone */
    if (check_timer(tone_time, tone_duration)) {
        tone_on = 0;
        tone_time = 0;
        tone_hw_disable();  // set both pins to input when off
    }

}

/*
 * delay - wait a bit.  this simply busy-waits, so it shouldn't
 *  be used much, if ever.
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
 * wiggling light pattern, to show life at startup.  this is
 * useful for visually detecting a watchdog reset or crash.  uses
 * a delay loop -- so don't call it after the system is up and
 * running.
 */
void blinky(void)
{
    byte i;
    for (i = 0; i < 12; i++) {
        led1_flip();
        delay(50);
    }
}

void do_debug_out(void)
{
    /* a square wave is useful for debugging baud rate issues */
    while (1) putch('U');

    /* not reached */
}


void util_init(void)
{
#ifdef USE_PRINTF
    stdout = &mystdout;
#endif
}

// vile:noti:sw=4
