
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include "common.h"
#include "timer.h"
#include "util.h"
#include "blind.h"
#include "ir.h"

#define PORTMOTOR PORTA
#define PINMOTOR PINA
#define DDRMOTOR DDRA
#define P_MOTOR_ON	PA0
#define P_MOTOR_DIR	PA1
#define P_MOTOR_TURN	PA2
#define P_LIMIT		PA3

static char cur_rotation;
#define SET_CW  0
#define SET_CCW 1

#define MOTOR_STOPPED	0
#define MOTOR_CW	1
#define MOTOR_CCW	2
#define MOTOR_REVERSE	3
static char motor_cur, motor_next;
static long motor_state_timer;

#define BLIND_STOPPED          0
#define BLIND_FALLING          1
#define BLIND_AT_LIMIT         2
#define BLIND_RISING           3
#define BLIND_AT_TOP_STOP      4
#define BLIND_AT_BOTTOM_STOP   5
#define BLIND_FORCE_RISING     6
#define BLIND_FORCE_FALLING    7

static char blind_cur, blind_next;

static int pulses;
static int pulsegoal;
static int ignore_limit;
static int top_stop;
static int bottom_stop;
int get_pulses(void);
void zero_pulses(void);

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

/*
 * the LOW_IDLE stopping position is a position near the
 * limit switch, but not on it.  it's easier to hit
 * the limit and back off while we still know what direction
 * we're going than it is to leave the limit as we're starting up.
 * 
 */

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

    zero_pulses();
    
    pulsegoal = inch_to_pulse(10);

    top_stop = NOMINAL_PEAK;
    bottom_stop = -10000;
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
	    zero_pulses();
	    blind_cur = BLIND_AT_LIMIT;
	} else if (get_pulses() >= top_stop) {
	    stop_moving();
	    blind_cur = BLIND_AT_TOP_STOP;
	} else if (get_pulses() <= bottom_stop) {
	    stop_moving();
	    blind_cur = BLIND_AT_BOTTOM_STOP;
	} else
#endif
	if (blind_next == BLIND_RISING) {
	    start_moving_up();
	    blind_cur = BLIND_RISING;
	    pulsegoal = top_stop;
	} else if (blind_next == BLIND_FALLING) {
	    start_moving_down();
	    blind_cur = BLIND_FALLING;
	    pulsegoal = bottom_stop;
	}
	break;

    case BLIND_FALLING:
	if (at_limit()) {
	    stop_moving();
	    zero_pulses();
	    blind_cur = BLIND_AT_LIMIT;
	} else if (blind_next == BLIND_RISING) {
	    start_moving_up();
	    blind_cur = BLIND_RISING;
	    pulsegoal = top_stop;
	} else if (get_pulses() == pulsegoal) {
	    stop_moving();
	    blind_cur = BLIND_AT_BOTTOM_STOP;
	}
	break;

    case BLIND_AT_LIMIT:
	if (blind_next == BLIND_RISING) {
	    start_moving_up();
	    blind_cur = BLIND_RISING;
	    ignore_limit = inch_to_pulse(2);
	    pulsegoal = top_stop;
	}
	break;

    case BLIND_RISING:
	if (ignore_limit)
	    ignore_limit--;
	if (at_limit() && !ignore_limit) {
	    stop_moving();
	    zero_pulses();
	    blind_cur = BLIND_AT_LIMIT;
	} else if (blind_next == BLIND_FALLING) {
	    start_moving_down();
	    blind_cur = BLIND_FALLING;
	    pulsegoal = bottom_stop;
	} else if (get_pulses() == pulsegoal) {
	    stop_moving();
	    blind_cur = BLIND_AT_TOP_STOP;
	}
	break;

    case BLIND_AT_TOP_STOP:
	if (blind_next == BLIND_FALLING) {
	    start_moving_down();
	    blind_cur = BLIND_FALLING;
	    pulsegoal = bottom_stop;
	} else if (blind_next == BLIND_FORCE_RISING) {
	    start_moving_up();
	    blind_cur = BLIND_RISING;
	    pulsegoal = top_stop + inch_to_pulse(6);
	}
	break;

    case BLIND_AT_BOTTOM_STOP:
	if (blind_next == BLIND_RISING) {
	    start_moving_up();
	    blind_cur = BLIND_RISING;
	    pulsegoal = top_stop;
	} else if (blind_next == BLIND_FORCE_FALLING) {
	    start_moving_down();
	    blind_cur = BLIND_FALLING;
	    pulsegoal = bottom_stop - inch_to_pulse(3);
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
	case MOTOR_CCW:	    motor_next = MOTOR_CW;  break;
	case MOTOR_CW:	    motor_next = MOTOR_CCW;  break;
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

ISR(INT1_vect)		// rotation pulse
{
    if (get_direction())
	pulses++;
    else
	pulses--;
}

int get_pulses(void)
{
    int p;

    cli();
    p = pulses;
    sei();

    return p;
}

void zero_pulses(void)
{
    cli();
    pulses = 0;
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
	top_stop = get_pulses();
	break;

    case BL_SET_BOTTOM:
	bottom_stop = get_pulses();
	break;

    case BL_FORCE_UP:
	blind_next = BLIND_FORCE_RISING;
	break;

    case BL_FORCE_DOWN:
	blind_next = BLIND_FORCE_FALLING;
	break;
    }
}
