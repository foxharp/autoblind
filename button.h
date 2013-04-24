/*
 * Copyright (c) 2013 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file
 * for details.
 */

/*
 * pushbutton gpio pin
 */
#define BUTTON_PORT         PORTB
#define BUTTON_PIN          PINB
#define BUTTON_BIT          PB2 // input:  from pushbutton
#define read_button()       !(BUTTON_PIN & bit(BUTTON_BIT))

void button_process(void);
void button_init(void);
char get_button(void);

enum {
    BUTTON_SHORT = 1,
    BUTTON_LONG,
};
