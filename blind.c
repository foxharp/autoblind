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

/*
 * two different state machines drive the window blind.
 * 1) the lower level state machine is in charge of the motor,
 *   and attempts to keep it from being turned on and off too
 *   quickly, or thrown from forward into reverse while running,
 *   etc.  save mechanical operation, in other words.
 * 2) the higher level state machine represents the motion of the
 *   blind, and controls its operation based on user input, soft
 *   and "hard" limits, etc.  it calls into the motor state machine
 *   to actually do anything.
 */

#define STATE_DEBUG 0
#define MOTOR_DEBUG 0

#define PORTMOTOR PORTA
#define PINMOTOR PINA
#define DDRMOTOR DDRA
#define P_MOTOR_ON      PA0
#define P_MOTOR_DIR     PA1
#define P_MOTOR_TURN    PA2
#define P_LIMIT         PA3


/*
 * for the motor state machine, we set motor_next to the state we
 * wish the motor to advance to.  the state machine churns until
 * motor_cur is equal to motor_next, and then it stops changing.
 */
enum {
    MOTOR_STOPPED = 0,
    MOTOR_STOPPING,
    MOTOR_BRAKING,
    MOTOR_UP,
    MOTOR_DOWN,
    MOTOR_REVERSE,
};
static char motor_cur, motor_next;
static long motor_state_timer;

/*
 * the blind state machine is a little different.  "blind_do" can
 * be thought of as an event, and "blind_is" represents the current
 * state of the blind.  the state machine reacts to "blind_do", causing
 * changes in "blind_is".
 */
enum {
    BLIND_STOP = 0,
    BLIND_UP,
    BLIND_DOWN,
    BLIND_FORCE_UP,
    BLIND_FORCE_DOWN,
    BLIND_TOGGLE,
    BLIND_NOP,
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


// our desired position
static int goal;

// once we hit the limit switch, we stop, and wouldn't be able to
// start again.  we allow ourselves to move a distance of
// "ignore_limit" before paying attention to the switch again.
static int ignore_limit;    

// we want to save our more recent position in non-volatile memory,
// so that we still know where the blind is after a power failure.
// we do the save 30 seconds after the last movement stopped.
static long position_change_timer;
static char position_changed;

// this structure describes the data stored in non-volatile memory.
// best not to rearrange it, but unless the "keep eeprom" fuse is
// burnt in the processor, then the EEPROM is erased when a new
// program is written anyway, so it doesn't matter much.
struct blind_config {
    int top_stop;
    int bottom_stop;
    int position;
    int up_dir;
    int padding[4];
} blc[1];

/*
 * convert spool revolutions to inches of travel of the blind:
 *   inches = pulses * diameter * pi
 *   pulses = inches / (diameter * pi)
 *   pulses = inches / ( 3.15 * .2)
 *   pulses = inches / .63 
 */
#define inch_to_pulse(in)  (100 * (in) / 63)

#define NOMINAL_PEAK inch_to_pulse(18)

// this is where user commands end up, whether from IR, the
// button, or from monitor().
char blind_cmd;

// we print the blind's position to the serial port once per second
char do_blind_report;

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

char blind_at_limit(void)
{
    /* detect the limit switch */
    return !(PINMOTOR & bit(P_LIMIT));
}


/* non-volatile memory -- read and save eeprom */

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
    if (blc->up_dir == 0xffff)
        blc->up_dir = 0;

    // write back any updated values
    blind_save_config();
}

void blind_save_config(void)
{
    putstr("saving config\n");
    eeprom_update_block((void *)blc, (void *)0, sizeof(*blc));
}


/* initilization */
void blind_init(void)
{
    PORTMOTOR &= ~(bit(P_MOTOR_ON) | bit(P_MOTOR_DIR));
    DDRMOTOR |= bit(P_MOTOR_ON) | bit(P_MOTOR_DIR);

    PORTMOTOR |= bit(P_LIMIT); // enable pullup

    // clear and enable motor rotation interrupt
    GIFR = bit(INTF1);
    GIMSK |= bit(INT1);

    set_motion(0);

    set_direction(0);

    goal = inch_to_pulse(10);
}


/* manage the shaft rotation interrupt, which lets us keep
 * track of where the blind is.
 */
static int get_position(void)
{
    int p;

    cli();
    p = blc->position;
    sei();

    return p;
}

void blind_set_position(int p) // mainly for debug, for use from monitor
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
    if (get_direction() == blc->up_dir)
        blc->position++;
    else
        blc->position--;

    if (ignore_limit)
        ignore_limit--;

    position_change_timer = get_ms_timer();
    position_changed = 1;
}


/* control the motor, both on/off and direction */
static void stop_moving(void)
{
    putstr("stop_moving\n");
    motor_next = MOTOR_STOPPED;
}

static void start_moving_up(void)
{
    putstr("start moving up\n");
    motor_next = MOTOR_UP;
}

static void start_moving_down(void)
{
    putstr("start moving down\n");
    motor_next = MOTOR_DOWN;
}


/*
 * the blind state machine
 */
static void blind_state(void)
{
    static char recent_motion;
    char next_do = BLIND_NOP;

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
        blind_do = BLIND_NOP;
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
                next_do = BLIND_DOWN;
            } else {
                next_do = BLIND_UP;
            }
        }
        break;

    case BLIND_IS_FALLING:
        recent_motion = blind_is;
        if (blind_at_limit()) {
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
            next_do = BLIND_STOP;
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
        if (blind_at_limit() && !ignore_limit) {
            putstr("hit LIMIT\n");
            stop_moving();
            zero_position();
            blind_is = BLIND_IS_AT_LIMIT;
            // we hit the limit going the wrong direction 
            blc->up_dir = !blc->up_dir;
            blind_save_config();
        } else if (blind_do == BLIND_DOWN) {
            start_moving_down();
            blind_is = BLIND_IS_FALLING;
            goal = blc->bottom_stop;
        } else if (get_position() == goal) {
            stop_moving();
            blind_is = BLIND_IS_AT_TOP_STOP;
        } else if (blind_do == BLIND_TOGGLE) {
            next_do = BLIND_STOP;
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
            goal = get_position() + inch_to_pulse(8);
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
            goal = get_position() - inch_to_pulse(18);
        }
        break;
    }

    blind_do = next_do;
}


/*
 * the motor state machine
 */
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
        case MOTOR_DOWN:    motor_next = MOTOR_UP;  break;
        case MOTOR_UP:      motor_next = MOTOR_DOWN;  break;
        case MOTOR_STOPPED: motor_next = MOTOR_STOPPED; break;
        }
    }

    if (motor_next == motor_cur) {
        return;
    }

    switch (motor_cur) {
    case MOTOR_DOWN:
    case MOTOR_UP:
        // if we're currently moving, then no matter what,
        // we stop first.
        set_motion(0);

        motor_cur = MOTOR_STOPPING;

        // schedule the next transition
        motor_state_timer = get_ms_timer();
        break;

    case MOTOR_STOPPING:
        // wait for prior transitions to complete
        if (check_timer(motor_state_timer, 50)) {
            // then we reverse the direction relay to force a stop
            set_direction(!get_direction());

            motor_cur = MOTOR_BRAKING;
            // schedule the next transition
            motor_state_timer = get_ms_timer();
        }
        break;
    case MOTOR_BRAKING:
        // wait for prior transitions to complete
        if (check_timer(motor_state_timer, 50)) {
            // then we idle the direction relay
            set_direction(0);

            motor_cur = MOTOR_STOPPED;
            // schedule the next transition
            motor_state_timer = get_ms_timer();
        }
        break;

    case MOTOR_STOPPED:
        // wait for prior transitions to complete
        if (check_timer(motor_state_timer, 50)) {

            // we're stopped, and want to start.  set direction first
            if (motor_next == MOTOR_UP && get_direction() != blc->up_dir) {
                set_direction(blc->up_dir);
                break;
            }
            if (motor_next == MOTOR_DOWN && get_direction() != !blc->up_dir) {
                set_direction(!blc->up_dir);
                break;
            }
            // we're stopped, and the direction is set.  let's go!
            if (motor_next != MOTOR_STOPPED) {
                motor_cur = motor_next;
                set_motion(1);
            }
            // schedule the next transition
            motor_state_timer = get_ms_timer();
        }
        break;
    }
}

/*
 * translate incoming IR remote button presses to blind commands
 */
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
            if (alt && !check_timer(alt_timer, 1000)) {
                if (alt == 1)
                    do_blind_cmd(BL_FORCE_UP);
                else
                    do_blind_cmd(BL_SET_TOP);
                tone_start(TONE_CONFIRM);
            } else {
                do_blind_cmd(BL_GO_UP);
            }
            break;

    case 1: // down
            if (alt && !check_timer(alt_timer, 1000)) {
                if (alt == 1)
                    do_blind_cmd(BL_FORCE_DOWN);
                else
                    do_blind_cmd(BL_SET_BOTTOM);
                tone_start(TONE_CONFIRM);
            } else {
                do_blind_cmd(BL_GO_DOWN);
            }
            break;

    case 4: // stop  (center)
            do_blind_cmd(BL_STOP);
            break;

    case 5: // alt  (power)
            if (alt && !check_timer(alt_timer, 1000)) {
                alt++;
            } else {
                alt = 1;
            }

            tone_start(TONE_CHIRP);
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

/*
 * get commands and drive the blind and motor state machine
 */
void blind_process(void)
{

    char cmd;

    // save current position 30 seconds after it stops changing
    if (position_changed &&
            check_timer(position_change_timer, 30*1000)) {
        blind_save_config();
        position_changed = 0;
    }

    if (do_blind_report) {
        static int last_pos;
        int cur_pos;

        cur_pos = get_position();
        if (cur_pos != last_pos) {
            p_hex(get_position());
            last_pos = cur_pos;
        }
        do_blind_report = 0;
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
        blind_save_config();
        break;

    case BL_SET_BOTTOM:
        blc->bottom_stop = get_position();
        blind_save_config();
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
