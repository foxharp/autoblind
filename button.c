/*
 * Copyright 2013 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file
 * for details.
 *
 * -----------
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include "common.h"
#include "timer.h"
#include "util.h"
#include "blind.h"
#include "button.h"


#define BUTTON_DEBUG 0

/* states */
enum {
    BUTTON_IS_UP = 0,
    BUTTON_IS_DEBOUNCING,
    BUTTON_IS_DOWN,
};

void button_init(void)
{
    BUTTON_PORT |= bit(BUTTON_BIT); // enable pullup
}

void button_process(void)
{
    static char button_is;
    static long button_timer;

    char button;

    button = read_button();

    if (BUTTON_DEBUG) {
        static char last_button_is;
        if (last_button_is != button_is) {
            p_hex(button_is);
            last_button_is = button_is;
        }
    }

    // debouncing
    switch (button_is) {
    case BUTTON_IS_UP:
        // Button depressed (OEMIO returns asserted/not asserted)
        if (button) {
            putstr("button debounce\n");
            button_timer = get_ms_timer();
            button_is = BUTTON_IS_DEBOUNCING;
        }
        break;

    case BUTTON_IS_DEBOUNCING:
        if (!button) {
            button_is = BUTTON_IS_UP;
            break;
        }

        if (check_timer(button_timer, 50)) {
            button_is = BUTTON_IS_DOWN;
            // putstr("button assert\n");
        }
        break;

    case BUTTON_IS_DOWN:
        if (button)  // still down
            break;

        // released -- all actions happen on release
        button_is = BUTTON_IS_UP;

        // check for a long press first
        if (check_timer(button_timer, 1000)) {
            putstr("long button\n");
            // do nothing yet
            break;
        }


        // short press
        putstr("short button\n");
        do_blind_cmd(BL_ONE_BUTTON);
        break;

    }
}

// vile:noti:sw=4
