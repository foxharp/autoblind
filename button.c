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

/*
 * handle the pushbutton, including debouncing, and differentiating
 * long pushes (more than a second) from short ones.
 */

#define BUTTON_DEBUG 0  // set to 1 to enable state machine debug output

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
    static char button_state;
    static long button_timer;

    char button_down;

    button_down = read_button();

    if (BUTTON_DEBUG) {
        static char last_button_state;
        if (last_button_state != button_state) {
            p_hex(button_state);
            last_button_state = button_state;
        }
    }

    switch (button_state) {
    case BUTTON_IS_UP:
        if (button_down) {
            // putstr("start button debounce\n");
            button_timer = get_ms_timer();
            button_state = BUTTON_IS_DEBOUNCING;
        }
        break;

    case BUTTON_IS_DEBOUNCING:
        if (!button_down) {  // button went up too soon
            button_state = BUTTON_IS_UP;
            break;
        }

        if (check_timer(button_timer, 50)) { // it's been down 50ms
            button_state = BUTTON_IS_DOWN;
            // putstr("button asserted\n");
        }
        break;

    case BUTTON_IS_DOWN:
        if (button_down)  // still down
            break;

        // button was released -- all actions happen when it's released.
        button_state = BUTTON_IS_UP;

        // check for a long press first
        if (check_timer(button_timer, 1000)) {
            putstr("long button\n");
            do_blind_cmd(BL_GO_BOTTOM);
            break;
        }

        // short press
        putstr("short button\n");
        do_blind_cmd(BL_ONE_BUTTON);
        break;

    }
}

// vile:noti:sw=4
