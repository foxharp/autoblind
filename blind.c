
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include "common.h"
#include "util.h"
#include "blind.h"
#include "ir.h"

char cur_rotation;

#define PORTMOTOR PORTA
#define PINMOTOR PINA
#define DDRMOTOR DDRA
#define P_MOTOR_ON	PA0
#define P_MOTOR_DIR	PA1
#define P_MOTOR_TURN	PA2
#define P_LIMIT		PA3

#define NOT_MOVING 0
#define MOVING_UP 1
#define MOVING_DOWN 2
#define BOUNCEBACK 2
char motion;
char atpeak;

#define LEAVING_LIMIT 10
#define LOW_IDLE (LEAVING_LIMIT * 3)
int pulses;
int pulsegoal;
int peak;

#define NOMINAL_PEAK 200

char blind_cmd;

#define post_stop_delay() // sleep(1)

/* commands come from either local buttons or serial */
char
get_local_cmd(void)
{
    return 0;
}

/* I/O -- read the limit switch, control the motors */
int
hitlimit(void)
{
    /* detect the limit switch */
    return PINMOTOR & bit(P_LIMIT);
}

void
set_motion(int on)
{
    /* set motor run state bit */
    if (on)
	PORTMOTOR |= bit(P_MOTOR_ON);
    else
	PORTMOTOR &= ~bit(P_MOTOR_ON);
}

void
set_direction(int cw)
{
    /* set rotation bit */
    if (cw)
	PORTMOTOR |= bit(P_MOTOR_DIR);
    else
	PORTMOTOR &= ~bit(P_MOTOR_DIR);
}

/*
 * the LOW_IDLE stopping position is a position near the
 * limit switch, but not on it.  it's easier to hit
 * the limit and back off while we still know what direction
 * we're going than it is to leave the limit as we're starting up.
 * 
 */

void
blind_init(void)
{
    PORTMOTOR &= ~(bit(P_MOTOR_ON) | bit(P_MOTOR_DIR));
    DDRMOTOR |= bit(P_MOTOR_ON) | bit(P_MOTOR_DIR);

    // clear and enable motor rotation interrupt
    GIFR = bit(INTF1);
    GIMSK |= bit(INT1);

    set_motion(0);
    set_direction(0);
    pulses = pulsegoal = LOW_IDLE;
    cur_rotation = 0;

    peak = NOMINAL_PEAK;
    motion = NOT_MOVING;
    atpeak = 0;
}

void
stop_moving(void)
{
    set_motion(0);
    post_stop_delay();
}

void
start_moving(void)
{
    set_direction(cur_rotation);
    set_motion(1);
}

ISR(INT1_vect)		// limit switch
{
    pulses++;
    if (hitlimit() && pulses > LEAVING_LIMIT) {

	if (motion != BOUNCEBACK) {
	    stop_moving();
	    cur_rotation = !cur_rotation;
	}

	if (motion == MOVING_DOWN) {
	    peak = pulses/2;
	    // we want to move back up off the limit
	    pulsegoal = LOW_IDLE;
	    motion = BOUNCEBACK;
	}
	start_moving();
	pulses = 0;
    } else  if (motion == MOVING_UP || motion == BOUNCEBACK) {

	if (pulses >= pulsegoal) {
	    stop_moving();
	    if (motion == MOVING_UP)
		atpeak = 1;
	    motion = NOT_MOVING;
	}
    }
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

char
blind_get_cmd()
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

    cmd = blind_get_cmd();
    if (!cmd)
	return;

    switch (cmd) {
    case BL_STOP:
	stop_moving();
	break;

    case BL_GO_UP:
	if (atpeak)
	    break;

	pulsegoal = peak;
	start_moving();
	break;

    case BL_GO_DOWN:
	start_moving();
	break;
    }
}
