
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include "common.h"
#include "util.h"
#include "blind.h"
#include "ir.h"

char cur_rotation;

#define NOT_MOVING 0
#define MOVING_UP 1
#define MOVING_DOWN 2
#define STOPPING 2
char moving;
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

char
get_serial_cmd(void)
{
    return 0;
}


/* I/O -- read the limit switch, control the motors */
int
hitlimit(void)
{
    /* detect the limit switch */
    return 0;
}

void
set_motion(int on)
{
    /* set motor run state bit */
}

void
set_direction(int cw)
{
    /* set rotation bit */
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
    set_motion(0);
    pulses = pulsegoal = LOW_IDLE;
    cur_rotation = 0;

    peak = NOMINAL_PEAK;
    atpeak = 0;
    moving = NOT_MOVING;
}

void
stop_moving(void)
{
    set_motion(0);
    post_stop_delay();
}

void
start_moving(int type)
{
    set_direction(cur_rotation);
    set_motion(1);
}

void
pulse_isr(void)
{
    pulses++;
    if (hitlimit() && pulses > LEAVING_LIMIT) {
	if (moving != STOPPING) {
	    stop_moving();
	    cur_rotation = !cur_rotation;
	}
	if (moving == MOVING_DOWN) {
	    peak = pulses/2;
	    pulsegoal = LOW_IDLE;
	    moving = STOPPING;
	}
	start_moving(moving);
	pulses = 0;
	return;
    } 
    
    if (moving == MOVING_UP || moving == STOPPING) {
	if (pulses >= pulsegoal) {
	    stop_moving();
	    moving = NOT_MOVING;
	    atpeak = 1;
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
	start_moving(MOVING_UP);
	break;

    case BL_GO_DOWN:
	start_moving(MOVING_DOWN);
	break;
    }
}
