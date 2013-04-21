/*
 * Copyright (c) 2013 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file
 * for details.
 */

void ir_process(void);
void ir_show_code(void);
void ir_init(void);
char get_ir(void);

extern char ir_code_avail;
#define ir_avail()  (ir_code_avail)

enum {
    IR_TOP,
    IR_MIDDLE,
    IR_BOTTOM,
    IR_STOP,
    IR_ALT,
};
// vile:noti:sw=4
