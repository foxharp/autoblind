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
#include <avr/wdt.h>
#include <avr/power.h>

#include "suart.h"
#include "common.h"
#include "timer.h"
#include "ir.h"
#include "blind.h"
#include "button.h"
#include "util.h"

#if ALL_STRINGS_PROGMEM
// override default __do_copy_data(), since we build with
// a linker script that leaves data in flash.
void __do_copy_data(void) { }
#endif

volatile byte mcusr_mirror;
void get_mcusr(void)
{
    mcusr_mirror = MCUSR;
    MCUSR = 0;
    wdt_disable();
}

void hardware_setup(void)
{

    // eliminate div-by-8 (no-op if 'div by 8' clock fuse not programmed)
    CLKPR = bit(CLKPCE);
    CLKPR = 0;

    // disable analog comparator -- saves power
    ACSRA = bit(ACD);

}

int main()
{
    get_mcusr();
    init_led();
    blind_init();
    blinky();

    hardware_setup();

    util_init();
    button_init();
    init_timer();
    suart_init();
    ir_init();

    sei();

    blind_read_config();

    if (read_button()) do_debug_out();   // no return

    wdt_enable(WDTO_4S);

    while (1) {
        wdt_reset();
#ifndef NO_MONITOR
        monitor();
#endif
        led_handle();
        tone_handle();
        ir_process();
        button_process();

        blind_process();
    }

}

// vile:noti:sw=4
