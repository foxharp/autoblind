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

#define PORTMOTOR PORTA
#define PINMOTOR PINA
#define DDRMOTOR DDRA
#define P_MOTOR_ON      PA0
#define P_MOTOR_DIR     PA1
#define P_MOTOR_TURN    PA2
#define P_LIMIT         PA3

enum {
    SET_CW = 0,
    SET_CCW
};
static char cur_rotation;

enum {
    MOTOR_STOPPED = 0,
    MOTOR_CW,
    MOTOR_CCW,
    MOTOR_REVERSE,
};
static char motor_cur, motor_next;
static long motor_state_timer;

enum {
    BLIND_STOPPED = 0,
    BLIND_RISING,
    BLIND_FALLING,
    BLIND_FORCE_RISING,
    BLIND_FORCE_FALLING,
    BLIND_AT_TOP_STOP,
    BLIND_AT_BOTTOM_STOP,
    BLIND_AT_LIMIT,
};

static char blind_cur, blind_next;

static int goal;
static int ignore_limit;
int get_position(void);
void zero_position(void);

struct blind_config {
    int top_stop;
    int bottom_stop;
    int position;
    int padding[13];
} blc[1];

#define NOMINAL_PEAK 200

// inches = pulses * diameter * pi
// pulses = inches / (diameter * pi)
// pulses = in / ( 3.15 * .2)
// pulses = in / .63 
#define inch_to_pulse(in)  (100 * (in) / 63)

char blind_cmd;

/* I/O -- read the limit switch, control the motors */
void set_motion(int on)
{
    /* set motor run state bit */
    if (on)
        PORTMOTOR |= bit(P_MOTOR_ON);
    else
        PORTMOTOR &= ~bit(P_MOTOR_ON);
}

char get_motion(void)
{
    /* get on/off bit */
    return !!(PINMOTOR & bit(P_MOTOR_ON));
}

void set_direction(int cw)
{
    /* set rotation bit */
    if (cw)
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
    int *ip;

    eeprom_read_block(blc, 0, sizeof(*blc));

#if 1
    ip = (int *)blc;
    for (i = 0; i < 16; i++) {
	p_hex(i); p_hex(ip[i]); crnl();
    }
#endif

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
    eeprom_update_block((void *)blc, (void *)0, sizeof(*blc));
}

void blind_init(void)
{
    PORTMOTOR &= ~(bit(P_MOTOR_ON) | bit(P_MOTOR_DIR));
    DDRMOTOR |= bit(P_MOTOR_ON) | bit(P_MOTOR_DIR);

    // clear and enable motor rotation interrupt
    GIFR = bit(INTF1);
    GIMSK |= bit(INT1);

    set_motion(0);

    cur_rotation = SET_CW;
    set_direction(SET_CW);

    goal = inch_to_pulse(10);
}

void stop_moving(void)
{
    putstr("stop_moving\n");
    motor_next = MOTOR_STOPPED;
}

#if 0
void start_moving(void)
{
    putstr("start_moving");
    if (cur_rotation == SET_CW) {
        putstr(" up");
        motor_next = MOTOR_CW;
    } else {
        putstr(" down");
        motor_next = MOTOR_CCW;
    }
}
#endif

void start_moving_up(void)
{
    putstr("start moving up");
    motor_next = MOTOR_CW;
}

void start_moving_down(void)
{
    putstr("start moving down");
    motor_next = MOTOR_CCW;
}

void blind_state(void)
{
    if (1)
    {
        static char last_blind_cur = -1, last_blind_next = -1;
        if (blind_cur != last_blind_cur) {
            p_hex(blind_cur);
            last_blind_cur = blind_cur;
        }
        if (blind_next != last_blind_next) {
            p_hex(blind_next);
            last_blind_next = blind_next;
        }
    }

    if (blind_next == blind_cur)
        return;

    if (blind_next == BLIND_STOPPED) {
        stop_moving();
        blind_cur = BLIND_STOPPED;
        return;
    }

    if (motor_state_timer) // no changes while the motor is settling
        return;

    switch (blind_cur) {
    case BLIND_STOPPED:
        if (get_motion()) {
            putstr("failsafe STOP\n");
            set_motion(0);
        }
#if 0
        if (at_limit()) {
            stop_moving();
            zero_position();
            blind_cur = BLIND_AT_LIMIT;
        } else if (get_position() >= blc->top_stop) {
            stop_moving();
            blind_cur = BLIND_AT_TOP_STOP;
        } else if (get_position() <= blc->bottom_stop) {
            stop_moving();
            blind_cur = BLIND_AT_BOTTOM_STOP;
        } else
#endif
        if (blind_next == BLIND_RISING) {
            start_moving_up();
            blind_cur = BLIND_RISING;
            goal = blc->top_stop;
        } else if (blind_next == BLIND_FALLING) {
            start_moving_down();
            blind_cur = BLIND_FALLING;
            goal = blc->bottom_stop;
        }
        break;

    case BLIND_FALLING:
        if (at_limit()) {
            stop_moving();
            zero_position();
            blind_cur = BLIND_AT_LIMIT;
        } else if (blind_next == BLIND_RISING) {
            start_moving_up();
            blind_cur = BLIND_RISING;
            goal = blc->top_stop;
        } else if (get_position() == goal) {
            stop_moving();
            blind_cur = BLIND_AT_BOTTOM_STOP;
        }
        break;

    case BLIND_AT_LIMIT:
        if (blind_next == BLIND_RISING) {
            start_moving_up();
            blind_cur = BLIND_RISING;
            ignore_limit = inch_to_pulse(2);
            goal = blc->top_stop;
        }
        break;

    case BLIND_RISING:
        if (ignore_limit)
            ignore_limit--;
        if (at_limit() && !ignore_limit) {
            stop_moving();
            zero_position();
            blind_cur = BLIND_AT_LIMIT;
        } else if (blind_next == BLIND_FALLING) {
            start_moving_down();
            blind_cur = BLIND_FALLING;
            goal = blc->bottom_stop;
        } else if (get_position() == goal) {
            stop_moving();
            blind_cur = BLIND_AT_TOP_STOP;
        }
        break;

    case BLIND_AT_TOP_STOP:
        if (blind_next == BLIND_FALLING) {
            start_moving_down();
            blind_cur = BLIND_FALLING;
            goal = blc->bottom_stop;
        } else if (blind_next == BLIND_FORCE_RISING) {
            start_moving_up();
            blind_cur = BLIND_RISING;
            goal = blc->top_stop + inch_to_pulse(6);
        }
        break;

    case BLIND_AT_BOTTOM_STOP:
        if (blind_next == BLIND_RISING) {
            start_moving_up();
            blind_cur = BLIND_RISING;
            goal = blc->top_stop;
        } else if (blind_next == BLIND_FORCE_FALLING) {
            start_moving_down();
            blind_cur = BLIND_FALLING;
            goal = blc->bottom_stop - inch_to_pulse(3);
        }
        break;

    case BLIND_FORCE_FALLING:
    case BLIND_FORCE_RISING:
        /* can't happen */
        break;

    }

}

void motor_state(void)
{
    // ensure we make all direction changes while motor is stopped.
    // ensure we don't restart too soon after stopping.

    if (1)
    {
        static char last_motor_cur = -1, last_motor_next = -1;
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

    if (motor_state_timer && !check_timer (motor_state_timer, 200)) {
            return;
    }
    motor_state_timer = 0;

    if (motor_next == motor_cur)
        return;

    switch (motor_cur) {
    case MOTOR_CCW:
    case MOTOR_CW:
        motor_cur = MOTOR_STOPPED;
        set_motion(0);
        motor_state_timer = get_ms_timer();
        break;

    case MOTOR_STOPPED:
        if (motor_next == MOTOR_CW && cur_rotation != SET_CW) {
            cur_rotation = SET_CW;
            set_direction(SET_CW);
            motor_state_timer = get_ms_timer();
            break;
        }

        if (motor_next == MOTOR_CCW && cur_rotation != SET_CCW) {
            cur_rotation = SET_CCW;
            set_direction(SET_CCW);
            motor_state_timer = get_ms_timer();
            break;
        }

        if (motor_next != MOTOR_STOPPED) {
            motor_cur = motor_next;
            set_motion(1);
        }

    }
}

ISR(INT1_vect)          // rotation pulse
{
    if (get_direction())
        blc->position++;
    else
        blc->position--;
}

int get_position(void)
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

void zero_position(void)
{
    cli();
    blc->position = 0;
    sei();
}

void blind_ir(void)
{
    char cmd;

    if (!ir_avail())
        return;

    cmd = get_ir();
    puthex(cmd); crnl();

    switch (cmd) {
    case 0: // up
            blind_cmd = BL_GO_UP;
            break;
    case 1: // down
            blind_cmd = BL_GO_DOWN;
            break;
    case 4: // stop  (center)
            blind_cmd = BL_STOP;
            break;
    case 5: // mark  (power)
            blind_cmd = BL_SET_TOP;
            break;
    }
}

char blind_get_cmd()
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

    blind_ir();

    motor_state();

    blind_state();

    cmd = blind_get_cmd();
    if (!cmd)
        return;

    switch (cmd) {
    case BL_STOP:
        blind_next = BLIND_STOPPED;
        break;

    case BL_GO_UP:
        blind_next = BLIND_RISING;
        break;

    case BL_GO_DOWN:
        blind_next = BLIND_FALLING;
        break;

    case BL_SET_TOP:
        blc->top_stop = get_position();
        break;

    case BL_SET_BOTTOM:
        blc->bottom_stop = get_position();
        break;

    case BL_FORCE_UP:
        blind_next = BLIND_FORCE_RISING;
        break;

    case BL_FORCE_DOWN:
        blind_next = BLIND_FORCE_FALLING;
        break;
    }
}

// vile:noti:sw=4
