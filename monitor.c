/* vi: set sw=4 ts=4: */
/*
 * monitor.c - simple monitor
 *
 * Copyright (c) 2010 One Laptop per Child
 * Copyright (c) 2011 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file
 * for details.
 */

#include <ctype.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include "timer.h"
#include "common.h"
#include "suart.h"
#include "ir.h"
#include "blind.h"
#include "util.h"
#ifdef USE_PRINTF
#include <stdio.h>
#endif

#define ctrl(c) (c ^ 0x40)
#define DEL 0x7f
#define tohex(c) (((c) >= 'a') ? ((c) - 'a' + 10) : ((c) - '0'))


#define BANNER PROGRAM_VERSION "\n"

#ifndef NO_MONITOR
#ifndef MINIMAL_MONITOR

static unsigned char line[16];
static unsigned char l;
static unsigned int addr;
static unsigned char addr_is_data;


static char getline(void)
{
	char c;

	if (!kbhit())
		return 0;

	c = getch();

	// eat leading spaces
	if (l == 0 && c == ' ')
		return 0;

	// special for the +/-/= memory dump commands
	if (l == 0 && (c == '+' || c == '-' || c == '=')) {
		line[l++] = c;
		line[l] = '\0';
		putch('\r');			// retreat over the prompt
		return 1;
	}

	if (c == '\r' || c == '\n') {
		// done
		putch('\n');
		return 1;
	}

	putch(c);

	// backspace
	if (c == '\b' || c == DEL) {
		putch(' ');
		putch('\b');
		if (l > 0)
			l--;
		line[l] = '\0';
		return 0;
	}
	// accumulate the character
	if (l < (sizeof(line) - 2)) {
		line[l++] = c;
		line[l] = '\0';
		return 0;
	}
	// line too long
	if (isprint(c)) {
		putch('\b');
		putch(' ');
		putch('\b');
	}
	return 0;
}

static int gethex(void)
{
	int n = 0;
	while (isspace(line[l])) {
		l++;
	}
	while (isxdigit(line[l])) {
		n = (n << 4) | tohex(line[l]);
		l++;
	}
	return n;
}

static void prompt(void)
{
	l = 0;
	line[0] = '\0';
	putch('>');
}


void monitor(void)
{
	unsigned int i, n;
	unsigned char cmd;

	if (!getline())
		return;

	l = 0;
	cmd = line[l++];
	n = gethex();

	switch (cmd) {
	case '\0':
		break;

	case 'u': // up
		blind_cmd = BL_GO_UP;
		break;
	case 'd': // down
		blind_cmd = BL_GO_DOWN;
		break;
	case 's': // stop
		blind_cmd = BL_STOP;
		break;
	case 'f': // force up
		blind_cmd = BL_FORCE_UP;
		break;
	case 'F': // force down
		blind_cmd = BL_FORCE_DOWN;
		break;
	case 'm':
		blind_cmd = BL_SET_TOP;
		break;
	case 'M':
		blind_cmd = BL_SET_BOTTOM;
		break;

#if 0
	case 'D':  // calibrate the delay timer
	{
		int i;
		long then, now;
		then = get_ms_timer();
		short_delay(10000);
		short_delay(10000);
		short_delay(10000);
		short_delay(10000);
		short_delay(10000);
		short_delay(10000);
		short_delay(10000);
		short_delay(10000);
		short_delay(10000);
		short_delay(10000);
		now = get_ms_timer();
		puthex32(now - then); // ms/100000, or us/100 loops
		putstr(" usec/100 loops\n");
		crnl();

		then = get_ms_timer();
		for (i = 0; i < 1000; i++)
			usec_delay(1000);
		now = get_ms_timer();
		puthex32(now - then);
		putstr(" should be 1000 (0x3e8) ms\n");
		crnl();
	}
		break;
#endif
	case 'i':
		ir_show_code();
		break;

	case 'l':
		wdt_disable();
		while (1) {
		    cmd = getch();
		    if (cmd == '\r') cmd = '\n';
		    putch(cmd);
		}

	case 't':
		{
		static const char *teststring = "foobar";
		// printf("testing: 0x%x, %s\n", 64, "hello");
		p_hex(n);
		p_hex(n * 2); crnl();
		p_dec(n);
		p_dec(n * 2); crnl();
		p_dec(0);
		p_str(teststring); crnl();
		}
		break;

	case 'v':
		putstr(BANNER);
		break;

	case 'q':
#define QUICKFOX "The Quick Brown Fox Jumps Over The Lazy Dog\n"
		for (i = 0; i < 20; i++)
			putstr(QUICKFOX);
		break;

	case 'U':
		for (i = 0; i < (80 * 20); i++)
			putch('U');
		putch('\n');
		break;

	case 'e':
		// restarts the current image
		wdt_enable(WDTO_250MS);
		for (;;);
		break;

#if LATER
	case 'B':
		// yanks the h/w reset line (jumpered to PB1), which
		// takes us to the bootloader.
		DDRB = bit(PB1);
		PORTB &= ~bit(PB1);
		for (;;);
		break;
#endif

	case 'w':
		addr = gethex();
		n = gethex();
		*(unsigned char *) addr = n;
		break;

	case 'x':					//  read 1 byte from xdata
	case 'X':					//  read 1 byte from data
		addr = gethex();
		addr_is_data = (cmd == 'd');
		// fallthrough

	case '=':
	case '+':
	case '-':
		if (cmd == '+')
			addr++;
		else if (cmd == '-')
			addr--;

#if 0
		printf("%04x: %02x\n", (uint) addr,
			   addr_is_data ?
			   (uint) * (unsigned char *) addr :
			   (uint) * (unsigned char xdata *) addr);
#else
		puthex16(addr);
		putstr(": ");
		if (addr_is_data)
			puthex(*(unsigned char *) addr);
		else
			puthex(pgm_read_byte(addr));
		putch('\n');
#endif
		break;

	default:
		putch('?');
		crnl();
	}

	prompt();
}


#else

// smaller version, might be useful

void monitor(void)
{
	int i;
	char c;

	if (!kbhit())
		return;

	c = getch();

#if TEST_RX
	puthex(c);
	putch(',');
	putch(' ');
	return;
#endif


	switch (c) {
	case 'v':
		putstr(BANNER);
		break;

	case 'x':
		for (i = 0; i < 20; i++)
			putstr(QUICKFOX);
		break;

	case 'U':
		for (i = 0; i < (80 * 20); i++)
			putch('U');
		putch('\n');
		break;

	case 'z':
		for (i = 0; i < (80 * 20); i++)
			putch("0123456789abcdef"[(get_ms_timer() / 1000) & 0xf]);	/* last digit of time */
		putch('\n');
		break;

	case 'e':
		wdt_enable(WDTO_250MS);
		for (;;);
		break;

	}
}

#endif
#endif
