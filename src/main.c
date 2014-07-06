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

byte saved_mcusr;

void cpu_setup(void)
{

    // eliminate div-by-8 (no-op if 'div by 8' clock fuse not programmed)
    CLKPR = bit(CLKPCE);
    CLKPR = 0;

    // disable analog comparator -- saves power
    ACSRA = bit(ACD);

}

int main()
{
    saved_mcusr = MCUSR; // save the reset reason, in case we need it
    MCUSR = 0;
    wdt_disable(); // disable the watchdog early

    init_led();
    blinky();       // blinky just flashes the LED, to show we're alive

    cpu_setup();

    util_init();
    button_init();
    init_timer();
    suart_init();
    ir_init();
    blind_init();

    sei();

    putstr("\n" BANNER);

    blind_read_config();

    if (read_button()) do_debug_out();   // no return

    wdt_enable(WDTO_4S);

    /* this loop runs forever.  the routines called here
     * must not block -- they act via state machines that
     * react to interrupt events (i.e., input from the user
     * or senors) and timer expirations.
     */
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
