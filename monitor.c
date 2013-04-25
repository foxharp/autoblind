/*
 * monitor.c - simple monitor
 *
 * Copyright (c) 2010 One Laptop per Child
 * Copyright (c) 2013 Paul Fox, pgf@foxharp.boston.ma.us
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


/* accumulate an input line, terminated with either carriage
 * return or newline.  return true when line[] is complete.
 */
static char getline(void)
{
    char c;

    if (!getch_avail())  // don't block -- getch() would loop forever
        return 0;

    c = getch();

    // toss leading spaces
    if (l == 0 && c == ' ')
        return 0;

    // special for the +/-/= memory dump commands
    // these three single-character commands don't need newline
    if (l == 0 && (c == '+' || c == '-' || c == '=')) {
        line[l++] = c;
        line[l] = '\0';
        putch('\r');            // retreat over the prompt
        return 1;
    }

    if (c == '\r' || c == '\n') {
        // done -- we have a complete line
        putch('\n');
        return 1;
    }

    putch(c);

    // check for backspace
    if (c == '\b' || c == DEL) {
        putch(' ');
        putch('\b');
        if (l > 0)
            l--;
        line[l] = '\0';
        return 0;
    }

    // save the new character
    if (l < (sizeof(line) - 2)) {
        line[l++] = c;
        line[l] = '\0';
        return 0;
    }

    // if the line's too long, save nothing
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

    if (!getline())     // do nothing until we have a line of user input
        return;

    l = 0;
    cmd = line[l++];
    n = gethex();       // fetch a numeric argument.  n will be 0 if none.

    switch (cmd) {
    case '\0':
        break;

    case 'b': // blind debug
        blind_state_debug = n;
        blind_motor_debug = gethex();
        break;

    case 'u': // up
        do_blind_cmd(BL_GO_TOP);
        break;
    case 'm': // middle
        do_blind_cmd(BL_GO_MIDDLE);
        break;
    case 'd': // down
        do_blind_cmd(BL_GO_BOTTOM);
        break;
    case 's': // stop
        do_blind_cmd(BL_STOP);
        break;

    case 'o': // one button control
        do_blind_cmd(BL_ONE_BUTTON);
        break;

    case 'f': // force up
        do_blind_cmd(BL_FORCE_UP);
        break;
    case 'F': // force down
        do_blind_cmd(BL_FORCE_DOWN);
        break;

    case 'U':
        do_blind_cmd(BL_SET_TOP);
        break;
    case 'M':
        do_blind_cmd(BL_SET_MIDDLE);
        break;
    case 'D':
        do_blind_cmd(BL_SET_BOTTOM);
        break;

    case 'I':
        // invert sense of direction
        do_blind_cmd(BL_INVERT);
        break;

    case 'B':
        blind_save_config();
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
        p_hex(blind_at_limit());
        crnl();
        break;

    case 'L':
        wdt_disable();
        while (1) {
            cmd = getch();
            if (cmd == 3) break;  // ctrl-C
            if (cmd == '\r') cmd = '\n';
            putch(cmd);
        }
        break;

    case 't':
        if (n == 1)
            tone_start(TONE_CHIRP);
        else if (n == 2)
            tone_start(TONE_CONFIRM);
        else {
            tone_start(gethex(), n);
        }
        break;

    case 'T':
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

    case 'Q':
        // print repeating 'U', which is a square wave on the transmit line
        for (i = 0; i < (80 * 20); i++)
            putch('U');
        putch('\n');
        break;

    case 'e':
        // reboot us, using the watchdog
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
        // 'w addr data'
        addr = n;
        n = gethex();
        *(unsigned char *) addr = n;
        break;

    case 'x':                   //  read 1 byte of data
    case 'X':                   //  read 1 byte of code
        // 'x addr' or 'X addr'
        addr = n;
        addr_is_data = (cmd == 'x');
        // fallthrough

    case '=':
    case '+':
    case '-':
        // show the contents of the next (or previous) address
        if (cmd == '+')
            addr++;
        else if (cmd == '-')
            addr--;

        puthex16(addr);
        putstr(": ");
        if (addr_is_data)
            puthex(*(unsigned char *)addr);
        else
            puthex(pgm_read_byte(addr));
        putch('\n');
        break;

    default:
        putch('?');
        crnl();
    }

    prompt();
}


#else

// much smaller version, might be useful someday

void monitor(void)
{
    int i;
    char c;

    if (!getch_avail())
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
            putch("0123456789abcdef"[(get_ms_timer() / 1000) & 0xf]);   /* last digit of time */
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


// vile:noti:sw=4
