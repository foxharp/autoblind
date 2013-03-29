/*
 * Copyright (c) 2011,2013 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file
 * for details.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <avr/eeprom.h>
#include "common.h"
#include "timer.h"
#include "util.h"
#include "blind.h"
#include "ir.h"

#define STATE_DEBUG 1
#define MOTOR_DEBUG 0

#define PORTMOTOR PORTA
#define PINMOTOR PINA
#define DDRMOTOR DDRA
#define P_MOTOR_ON      PA0
#define P_MOTOR_DIR     PA1
#define P_MOTOR_TURN    PA2
#define P_LIMIT         PA3

enum {
    CW = 0,
    CCW = 1,
};

enum {
    MOTOR_STOPPED = 0,
    MOTOR_CW,
    MOTOR_CCW,
    MOTOR_REVERSE,
};
static char motor_cur, motor_next;
static long motor_state_timer;
static long position_change_timer;
static char position_changed;

enum {
    BLIND_STOP = 0,
    BLIND_UP,
    BLIND_DOWN,
    BLIND_FORCE_UP,
    BLIND_FORCE_DOWN,
    BLIND_TOGGLE,
};
static char blind_do;

enum {
    BLIND_IS_STOPPED = 0,
    BLIND_IS_RISING,
    BLIND_IS_FALLING,
    BLIND_IS_AT_TOP_STOP,
    BLIND_IS_AT_BOTTOM_STOP,
    BLIND_IS_AT_LIMIT,
};
static char blind_is;

static int goal;
static int ignore_limit;

struct blind_config {
    int top_stop;
    int bottom_stop;
    int position;
    int padding[5];
} blc[1];

#define NOMINAL_PEAK 200

// inches = pulses * diameter * pi
// pulses = inches / (diameter * pi)
// pulses = in / ( 3.15 * .2)
// pulses = in / .63 
#define inch_to_pulse(in)  (100 * (in) / 63)

char blind_cmd;

/* I/O -- read the limit switch, control the motors */
static void set_motion(int on)
{
    if (MOTOR_DEBUG) {
        static int last_on;
        if (on != last_on) {
            p_hex(on);
            last_on = on;
        }
    }

    /* set motor run state bit */
    if (on)
        PORTMOTOR |= bit(P_MOTOR_ON);
    else
        PORTMOTOR &= ~bit(P_MOTOR_ON);
}

static char get_motion(void)
{
    /* get on/off bit */
    return !!(PINMOTOR & bit(P_MOTOR_ON));
}

static void set_direction(int dir)
{
    if (MOTOR_DEBUG) {
        static int last_dir;
        if (dir != last_dir) {
            p_hex(dir);
            last_dir = dir;
        }
    }

    /* set rotation bit */
    if (dir)
        PORTMOTOR |= bit(P_MOTOR_DIR);
    else
        PORTMOTOR &= ~bit(P_MOTOR_DIR);
}

char get_direction(void)
{
    /* get rotation bit */
    return !!(PINMOTOR & bit(P_MOTOR_DIR));
}

char at_limit(void)
{
    /* detect the limit switch */
    return !!(PINMOTOR & bit(P_LIMIT));
}

void blind_read_config(void)
{
    int i;

    eeprom_read_block(blc, 0, sizeof(*blc));

    if (1) {
        int *ip;
        ip = (int *)blc;
        crnl();
        for (i = 0; i < sizeof(*blc)/sizeof(int); i++) {
	    p_hex(i); p_hex(ip[i]); crnl();
        }
    }

    // set sensible defaults
    if (blc->top_stop == 0xffff)
	blc->top_stop = NOMINAL_PEAK;
    if (blc->bottom_stop == 0xffff)
	blc->bottom_stop = -10000;
    if (blc->position == 0xffff)
	blc->position = 10;

    // write back any updated values
    blind_save_config();
}

void blind_save_config(void)
{
    putstr("saving config\n");
#ifdef LATER
    eeprom_update_block((void *)blc, (void *)0, sizeof(*blc));
#endif
}

void blind_init(void)
{
    PORTMOTOR &= ~(bit(P_MOTOR_ON) | bit(P_MOTOR_DIR));
    DDRMOTOR |= bit(P_MOTOR_ON) | bit(P_MOTOR_DIR);

    // clear and enable motor rotation interrupt
    GIFR = bit(INTF1);
    GIMSK |= bit(INT1);

    set_motion(0);

    set_direction(CW);

    goal = inch_to_pulse(10);
}

static int get_position(void)
{
    int p;

    cli();
    p = blc->position;
    sei();

    return p;
}

/* mainly for debug, for use from monitor */
void blind_set_position(int p)
{
    cli();
    blc->position = p;
    sei();
}

static void zero_position(void)
{
    cli();
    blc->position = 0;
    sei();
}

ISR(INT1_vect)          // rotation pulse
{
    if (get_direction())
        blc->position++;
    else
        blc->position--;

    position_change_timer = get_ms_timer();
    position_changed = 1;
}

static void stop_moving(void)
{
    putstr("stop_moving\n");
    motor_next = MOTOR_STOPPED;
}

static void start_moving_up(void)
{
    putstr("start moving up\n");
    motor_next = MOTOR_CW;
}

static void start_moving_down(void)
{
    putstr("start moving down\n");
    motor_next = MOTOR_CCW;
}

static void blind_state(void)
{
    static char recent_motion;

    if (STATE_DEBUG) {
        static char last_blind_is, last_blind_do;
        if (blind_is != last_blind_is) {
            p_hex(blind_is);
            last_blind_is = blind_is;
        }
        if (blind_do != last_blind_do) {
            p_hex(blind_do);
            last_blind_do = blind_do;
        }
    }

    // make sure this is always honored, and immediately
    if (blind_do == BLIND_STOP) {
        if (get_motion()) {
            stop_moving();
            blind_is = BLIND_IS_STOPPED;
        }
        return;
    }

    if (motor_next != motor_cur) { // no changes while the motor is settling
        return;
    }

    switch (blind_is) {
    case BLIND_IS_STOPPED:
        if (get_motion()) {
            putstr("failsafe STOP\n");
            set_motion(0);
        }
#if 0
        if (at_limit()) {
            stop_moving();
            zero_position();
            blind_is = BLIND_IS_AT_LIMIT;
        } else if (get_position() >= blc->top_stop) {
            stop_moving();
            blind_is = BLIND_IS_AT_TOP_STOP;
        } else if (get_position() <= blc->bottom_stop) {
            stop_moving();
            blind_is = BLIND_IS_AT_BOTTOM_STOP;
        } else
#endif
        if (blind_do == BLIND_UP) {
            start_moving_up();
            blind_is = BLIND_IS_RISING;
            goal = blc->top_stop;
        } else if (blind_do == BLIND_DOWN) {
            start_moving_down();
            blind_is = BLIND_IS_FALLING;
            goal = blc->bottom_stop;
        } else if (blind_do == BLIND_TOGGLE) {
            if (recent_motion == BLIND_IS_RISING) {
                blind_do = BLIND_DOWN;
            } else {
                blind_do = BLIND_UP;
            }
        }
        break;

    case BLIND_IS_FALLING:
        recent_motion = blind_is;
        if (at_limit()) {
            stop_moving();
            zero_position();
            blind_is = BLIND_IS_AT_LIMIT;
        } else if (blind_do == BLIND_UP) {
            start_moving_up();
            blind_is = BLIND_IS_RISING;
            goal = blc->top_stop;
        } else if (get_position() == goal) {
            stop_moving();
            blind_is = BLIND_IS_AT_BOTTOM_STOP;
        } else if (blind_do == BLIND_TOGGLE) {
            blind_do = BLIND_STOP;
        }
        break;

    case BLIND_IS_AT_LIMIT:
        if (blind_do == BLIND_UP ||
                blind_do == BLIND_TOGGLE) {
            start_moving_up();
            blind_is = BLIND_IS_RISING;
            ignore_limit = inch_to_pulse(2);
            goal = blc->top_stop;
        }
        break;

    case BLIND_IS_RISING:
        recent_motion = blind_is;
        if (ignore_limit)
            ignore_limit--;
        if (at_limit() && !ignore_limit) {
            stop_moving();
            zero_position();
            blind_is = BLIND_IS_AT_LIMIT;
        } else if (blind_do == BLIND_DOWN) {
            start_moving_down();
            blind_is = BLIND_IS_FALLING;
            goal = blc->bottom_stop;
        } else if (get_position() == goal) {
            stop_moving();
            blind_is = BLIND_IS_AT_TOP_STOP;
        } else if (blind_do == BLIND_TOGGLE) {
            blind_do = BLIND_STOP;
        }
        break;

    case BLIND_IS_AT_TOP_STOP:
        if (blind_do == BLIND_DOWN ||
                blind_do == BLIND_TOGGLE) {
            start_moving_down();
            blind_is = BLIND_IS_FALLING;
            goal = blc->bottom_stop;
        } else if (blind_do == BLIND_FORCE_UP) {
            start_moving_up();
            blind_is = BLIND_IS_RISING;
            goal = blc->top_stop + inch_to_pulse(6);
        }
        break;

    case BLIND_IS_AT_BOTTOM_STOP:
        if (blind_do == BLIND_UP ||
                blind_do == BLIND_TOGGLE) {
            start_moving_up();
            blind_is = BLIND_IS_RISING;
            goal = blc->top_stop;
        } else if (blind_do == BLIND_FORCE_DOWN) {
            start_moving_down();
            blind_is = BLIND_IS_FALLING;
            goal = blc->bottom_stop - inch_to_pulse(3);
        }
        break;
    }

}

static void motor_state(void)
{

    if (STATE_DEBUG)
    {
        static char last_motor_cur, last_motor_next;
        if (motor_cur != last_motor_cur) {
            p_hex(motor_cur);
            last_motor_cur = motor_cur;
        }
        if (motor_next != last_motor_next) {
            p_hex(motor_next);
            last_motor_next = motor_next;
        }
    }

    if (motor_next == MOTOR_REVERSE) {
        switch (motor_cur) {
        case MOTOR_CCW:     motor_next = MOTOR_CW;  break;
        case MOTOR_CW:      motor_next = MOTOR_CCW;  break;
        case MOTOR_STOPPED: motor_next = MOTOR_STOPPED; break;
        }
    }

    if (motor_next == motor_cur) {
        return;
    }

    // wait for prior transitions to complete
    if (check_timer(motor_state_timer, 200)) {

        switch (motor_cur) {
        case MOTOR_CCW:
        case MOTOR_CW:
            // if we're currently moving, then no matter what,
            // we stop first.
            motor_cur = MOTOR_STOPPED;
            set_motion(0);
            break;

        case MOTOR_STOPPED:
            // we're stopped, and want to start.  set direction first
            if (motor_next == MOTOR_CW && get_direction() != CW) {
                set_direction(CW);
                break;
            }
            if (motor_next == MOTOR_CCW && get_direction() != CCW) {
                set_direction(CCW);
                break;
            }

            // we're stopped, and the direction is set.  let's go!
            if (motor_next != MOTOR_STOPPED) {
                motor_cur = motor_next;
                set_motion(1);
            }
        }

        // schedule the next transition
        motor_state_timer = get_ms_timer();
    }
}

static void blind_ir(void)
{
    char cmd;
    static long alt_timer;
    static char alt;

    if (!ir_avail())
        return;

    cmd = get_ir();

    switch (cmd) {
    case 0: // up
            if (alt && !check_timer(alt_timer, 500)) {
                if (alt == 1)
                    blind_cmd = BL_FORCE_UP;
                else
                    blind_cmd = BL_SET_TOP;
                tone_start(TONE_CONFIRM);
            } else {
                blind_cmd = BL_GO_UP;
            }
            break;

    case 1: // down
            if (alt && !check_timer(alt_timer, 500)) {
                if (alt == 1)
                    blind_cmd = BL_FORCE_DOWN;
                else
                    blind_cmd = BL_SET_BOTTOM;
                tone_start(TONE_CONFIRM);
            } else {
                blind_cmd = BL_GO_DOWN;
            }
            break;

    case 4: // stop  (center)
            blind_cmd = BL_STOP;
            break;

    case 5: // alt  (power)
            if (alt && !check_timer(alt_timer, 500)) {
                alt++;
                tone_start(TONE_CHIRP);
            } else {
                alt = 1;
            }
            alt_timer = get_ms_timer();
            return;
    }
    alt_timer = 0;
    alt = 0;
}

static char blind_get_cmd(void)
{
    char cmd;

    if (!blind_cmd)
        return 0;

    cmd = blind_cmd;
    blind_cmd = 0;

    return cmd;
}

void blind_process(void)
{

    char cmd;

    // save current position 30 seconds after it stops changing
    if (position_changed &&
            check_timer(position_change_timer, (long)30*1000*1000)) {
        blind_save_config();
        position_changed = 0;
    }

    blind_ir();

    motor_state();

    blind_state();

    cmd = blind_get_cmd();
    if (!cmd)
        return;

    switch (cmd) {
    case BL_STOP:
        blind_do = BLIND_STOP;
        break;

    case BL_GO_UP:
        blind_do = BLIND_UP;
        break;

    case BL_GO_DOWN:
        blind_do = BLIND_DOWN;
        break;

    case BL_SET_TOP:
        blc->top_stop = get_position();
        break;

    case BL_SET_BOTTOM:
        blc->bottom_stop = get_position();
        break;

    case BL_FORCE_UP:
        blind_do = BLIND_FORCE_UP;
        break;

    case BL_FORCE_DOWN:
        blind_do = BLIND_FORCE_DOWN;
        break;

    case BL_ONE_BUTTON:
        blind_do = BLIND_TOGGLE;
        break;
    }
}

// vile:noti:sw=4
