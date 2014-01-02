/*
 * Copyright (c) 2013 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file
 * for details.
 */
extern char blind_state_debug;
extern char blind_motor_debug;
extern long motor_state_timer;

void blind_init(void);
void blind_process(void);
void blind_save_config(void);
void blind_read_config(void);
char blind_at_limit(void);
void dump_config(void);

enum {
    BL_STOP = 1,
    BL_GO_TOP,
    BL_GO_MIDDLE,
    BL_GO_BOTTOM,
    BL_SET_TOP,
    BL_SET_MIDDLE,
    BL_SET_BOTTOM,
    BL_FORCE_UP,
    BL_FORCE_DOWN,
    BL_ONE_BUTTON,
    BL_INVERT,
};

extern char blind_cmd;
#define do_blind_cmd(x)  do { blind_cmd = x; } while(0)
extern char position_report;
#define  blind_report() do { position_report = 1; } while(0)

// vile:noti:sw=4
