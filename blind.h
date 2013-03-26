/*
 * Copyright (c) 2013 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file
 * for details.
 */

void blind_init(void);
void blind_process(void);
void blind_save_config(void);
void blind_read_config(void);
void blind_set_position(int n);

enum {
    BL_STOP = 1,
    BL_GO_UP,
    BL_GO_DOWN,
    BL_SET_TOP,
    BL_SET_BOTTOM,
    BL_FORCE_UP,
    BL_FORCE_DOWN,
    BL_ONE_BUTTON,
};

extern char blind_cmd;

// vile:noti:sw=4
