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
#include "button.h"
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


#define PORTMOTOR PORTA
#define PINMOTOR PINA
#define DDRMOTOR DDRA
#define P_MOTOR_ON      PA0
#define P_MOTOR_DIR     PA1
#define P_MOTOR_TURN    PA2
#define P_LIMIT         PA3

#define MAXPOS 32000

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
    // MOTOR_REVERSE,
};
static char motor_cur, motor_next;
long motor_state_timer;

char blind_state_debug;
char blind_motor_debug;

/*
 * the blind state machine is a little different.  "blind_do" can
 * be thought of as an event, and "blind_is" represents the current
 * state of the blind.  the state machine reacts to "blind_do", causing
 * changes in "blind_is".
 */
enum {
    BLIND_STOP = 0,
    BLIND_TOP,
    BLIND_MIDDLE,
    BLIND_BOTTOM,
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
    BLIND_IS_AT_LIMIT,
};
static char blind_is;


// once we hit the limit switch, we stop, and wouldn't be able to
// start again.  we allow ourselves to move a distance of
// "ignore_limit" before paying attention to the switch again.
static int ignore_limit;    

// we want to save our more recent position in non-volatile memory,
// so that we still know where the blind is after a power failure.
// we do the save 30 seconds after the last movement stopped.
static long config_change_timer;
static char config_changed;

// our desired position
static int goal;


// this structure describes the data stored in non-volatile memory.
// best not to rearrange it, but unless the "keep eeprom" fuse is
// burnt in the processor, then the EEPROM is erased when a new
// program is written anyway, so it doesn't matter much.
struct blind_config {
    int magic;
    int top_stop;
    int middle_stop;
    int bottom_stop;
    int position;
    int up_dir;
    int magic2;
} blc[1];

/*
 * convert spool revolutions to (roughly) inches of travel of the
 * blind:
 *   inches = pulses * diameter * pi
 *   pulses = inches / (diameter * pi)
 *   pulses = inches / ( 3.15 * .2)
 *   pulses = inches / .63 
 */
#define inch_to_pulse(in)  (100 * (in) / 63)

#define NOMINAL_PEAK inch_to_pulse(18)

// this is where user commands end up, whether from IR, the
// button, or from monitor().  do_blind_cmd() puts them here.
char blind_cmd;

// we print the blind's position to the serial port once per second
char position_report;

/* I/O -- read the limit switch, control the motors */
static void set_motion(int on)
{
    if (blind_motor_debug) {
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
    if (blind_motor_debug) {
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
void dump_config(void)
{
    int i, *ip;
    ip = (int *)blc;
    crnl();
    for (i = 0; i < sizeof(*blc)/sizeof(int); i++) {
        p_hex(i); p_hex(ip[i]); crnl();
    }
}

/* request a timed config save */
void blind_save_config(void)
{
    config_change_timer = get_ms_timer();
    config_changed = 1;
}

/* really save the config */
void blind_save_config_real(void)
{
    print_tstamp();
    putstr("saving config\n");
    eeprom_update_block(blc, (void *)0, sizeof(*blc));
    dump_config();
}

void blind_read_config(void)
{

    eeprom_read_block(blc, 0, sizeof(*blc));

    dump_config();

    // set sensible defaults
    if (blc->magic != 0xdead || blc->magic2 != 0xcafe) {
        blc->top_stop = NOMINAL_PEAK;
        blc->middle_stop = -MAXPOS;
        blc->bottom_stop = -MAXPOS;
        blc->position = 10;
        blc->up_dir = 0;
        blc->magic = 0xdead;
        blc->magic2 = 0xcafe;
    
        // write back any updated values
        blind_save_config_real();
    }

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
#if NEEDED
static void blind_set_position(int p) // mainly for debug, for use from monitor
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
#endif

static int get_position(void)
{
    int p;

    cli();
    p = blc->position;
    sei();

    return p;
}

ISR(INT1_vect)          // rotation pulse
{
    if (get_direction() == blc->up_dir)
        blc->position++;
    else
        blc->position--;

    if (ignore_limit)
        ignore_limit--;

    blind_save_config();

}

void config_process(void)
{
    // save current position 30 seconds after it stops changing
    if (config_changed &&
            check_timer(config_change_timer, 30*1000)) {
        blind_save_config_real();
        config_changed = 0;
    }
}

void position_process(void)
{
    if (position_report) {
        static int last_pos;
        int cur_pos;

        cur_pos = get_position();
        if (cur_pos != last_pos) {
            print_tstamp();
            p_hex(get_position());
            last_pos = cur_pos;
        }
        position_report = 0;
    }
}

/* control the motor, both on/off and direction.  these
 * routines are the interface between the upper (blind) state
 * machine and the lower (motor) state machine.
 */
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
    static char recent_goal;
    int pos;
    
    if (blind_state_debug) {
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

    pos = get_position();

    if (blind_do == BLIND_TOGGLE) {
        if (blind_is != BLIND_IS_STOPPED) {
            blind_do = BLIND_STOP;
        } else if (recent_goal == BLIND_TOP) {
            blind_do = BLIND_MIDDLE;
        } else {
            blind_do = BLIND_TOP;
        }
    }

    /* handle incoming commands */
    if (blind_do != BLIND_NOP) {

        if (blind_do == BLIND_STOP) {
            // make sure STOP is always honored, and immediately
            stop_moving();
            blind_is = BLIND_IS_STOPPED;
            blind_do = BLIND_NOP;
            return;
        }

        if (blind_do == BLIND_TOP) {
            recent_goal = BLIND_TOP;
            goal = blc->top_stop;
        } else if (blind_do == BLIND_MIDDLE) {
            recent_goal = BLIND_MIDDLE;
            goal = blc->middle_stop;
        } else if (blind_do == BLIND_BOTTOM) {
            recent_goal = BLIND_BOTTOM;
            goal = blc->bottom_stop;
        } else if (blind_do == BLIND_FORCE_UP) {
            goal = get_position() + inch_to_pulse(18);
        } else if (blind_do == BLIND_FORCE_DOWN) {
            goal = get_position() - inch_to_pulse(18);
        }

        /* the actions for all commands (except BLIND_STOP) are
         * the same.  they just have different goals.  */
        if (pos < goal) {
            blind_is = BLIND_IS_RISING;
            if (blind_at_limit())
                ignore_limit = inch_to_pulse(2);
            start_moving_up();
        } else if (pos > goal && !blind_at_limit()) {
            blind_is = BLIND_IS_FALLING;
            start_moving_down();
        } else {
            blind_is = BLIND_IS_STOPPED;
            stop_moving();
        }

        blind_do = BLIND_NOP;
    }


    if (motor_next != motor_cur) { // no changes while the motor is settling
        return;
    }

    switch (blind_is) {
    case BLIND_IS_STOPPED:
        if (get_motion()) {  // just in case -- shouldn't happen
            putstr("failsafe STOP\n");
            set_motion(0);
        }
        break;
    case BLIND_IS_FALLING:
        if (blind_at_limit() || pos <= goal) {
            stop_moving();
            blind_is = BLIND_IS_STOPPED;
        }
        break;
    case BLIND_IS_RISING:
        if ((blind_at_limit() && !ignore_limit) || pos >= goal) {
            stop_moving();
            blind_is = BLIND_IS_STOPPED;
        }
        break;
    }

}


/*
 * the motor state machine
 */
static void motor_state(void)
{

    if (blind_state_debug)
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
        // wait for prior transitions to complete...
        if (check_timer(motor_state_timer, 50)) {
            // ...then we reverse the direction relay to force a stop
            set_direction(!get_direction());

            motor_cur = MOTOR_BRAKING;
            // schedule the next transition
            motor_state_timer = get_ms_timer();
        }
        break;
    case MOTOR_BRAKING:
        // wait for prior transitions to complete...
        if (check_timer(motor_state_timer, 50)) {
            // ...then we idle the direction relay
            set_direction(0);

            motor_cur = MOTOR_STOPPED;
            // schedule the next transition
            motor_state_timer = get_ms_timer();
        }
        break;

    case MOTOR_STOPPED:
        // wait for prior transitions to complete...
        if (check_timer(motor_state_timer, 50)) {

            // ...and now we're stopped, and want to start.
            // set direction first
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
 * a short button press will cause the blind to behave much
 * like a typical garage door opener does:  it will cycle through
 * moving-up/stop/moving-down/stop on each press.  the limits
 * in this case are the top and middle positions.  giving the button
 * a long press will take it to the bottom position.  the next
 * short press will take it back to the middle.
 */
static void blind_button(void)
{
    char button;

    button = get_button();
    if (!button)
        return;

    switch(button) {
    case BUTTON_SHORT:
        do_blind_cmd(BL_ONE_BUTTON);
        break;
    case BUTTON_LONG:
        do_blind_cmd(BL_GO_BOTTOM);
        break;
    }
}

/*
 * translate incoming IR remote button presses to blind commands.
 * commands are either single "normal" presses, or some number of
 * ALT buttons followed by a "normal" press.
 * we use just 5 IR buttons:  three should take us to the "top",
 * "middle", and "bottom" positions, as well as "stop" and "alt".
 */
static void blind_ir(void)
{
    char ir;
    static long alt_timer;
    static char alt;

    if (alt && check_timer(alt_timer, 1000)) {
        tone_start(TONE_ABORT);
        alt = 0;
    }

    ir = get_ir();
    if (!ir)
        return;

    switch (ir) {
    case IR_TOP:
            if (alt) {
                if (alt == 1) {      // alt top
                    do_blind_cmd(BL_FORCE_UP);
                } else if (alt == 2) { // alt alt top
                    do_blind_cmd(BL_SET_TOP);
                } else {
                    tone_start(TONE_ABORT);
                    break;
                }
                tone_start(TONE_CONFIRM);
            } else {
                do_blind_cmd(BL_GO_TOP);
            }
            break;

    case IR_MIDDLE:
            if (alt) {
                if (alt == 2) { // alt alt bottom
                    do_blind_cmd(BL_SET_MIDDLE);
                } else {
                    tone_start(TONE_ABORT);
                    break;
                }
                tone_start(TONE_CONFIRM);
            } else {
                do_blind_cmd(BL_GO_MIDDLE);
            }
            break;

    case IR_BOTTOM:
            if (alt) {
                if (alt == 1) {      // alt bottom
                    do_blind_cmd(BL_FORCE_DOWN);
                } else if (alt == 2) { // alt alt bottom
                    do_blind_cmd(BL_SET_BOTTOM);
                } else {
                    tone_start(TONE_ABORT);
                    break;
                }
                tone_start(TONE_CONFIRM);
            } else {
                do_blind_cmd(BL_GO_BOTTOM);
            }
            break;

    case IR_STOP:
            if (alt) {
                if (alt == 5) {     // alt alt alt alt alt stop
                    /* reset all stops */
                    blc->top_stop = MAXPOS;
                    blc->middle_stop = -MAXPOS;
                    blc->bottom_stop = -MAXPOS;
                    tone_start(TONE_CONFIRM);
                } else if (alt == 3) {     // alt alt alt stop
                    do_blind_cmd(BL_INVERT);
                    tone_start(TONE_CONFIRM);
                } else {
                    tone_start(TONE_ABORT);
                    break;
                }

            } else {
                do_blind_cmd(BL_STOP);
            }
            break;

    case IR_ALT:
            alt++;
            tone_start(TONE_CHIRP);
            alt_timer = get_ms_timer();
            return;
    }

    alt = 0;
}


/*
 * interpret external blind commands.  some are handled right
 * here, some are passed on to the state machine.
 */
void blind_commands(void)
{
    char cmd;

    if (!blind_cmd)
        return;

    cmd = blind_cmd;
    blind_cmd = 0;

    switch (cmd) {

    case BL_STOP:
        blind_do = BLIND_STOP;
        break;

    case BL_GO_TOP:
        blind_do = BLIND_TOP;
        break;

    case BL_GO_MIDDLE:
        blind_do = BLIND_MIDDLE;
        break;

    case BL_GO_BOTTOM:
        blind_do = BLIND_BOTTOM;
        break;



    case BL_SET_TOP:
        blc->top_stop = get_position();
        p_hex(blc->top_stop);
        blind_save_config();
        break;

    case BL_SET_MIDDLE:
        blc->middle_stop = get_position();
        p_hex(blc->middle_stop);
        blind_save_config();
        break;

    case BL_SET_BOTTOM:
        blc->bottom_stop = get_position();
        p_hex(blc->bottom_stop);
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

    // this command changes the motor's mapping of clockwise or
    // counter-clockwise to "blind up" and "blind down".
    case BL_INVERT:
        blc->up_dir = !blc->up_dir;
        blind_save_config();
        break;
    }
}

/*
 * get commands and drive the blind and motor state machine
 */
void blind_process(void)
{

    config_process();
    position_process();

    blind_ir();
    blind_button();
    blind_commands();

    motor_state();

    blind_state();

}

// vile:noti:sw=4
