/* vi: set sw=4 ts=4: */
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
#include "util.h"

volatile byte mcusr_mirror;

void hardware_setup(void)
{
	mcusr_mirror = MCUSR;
	MCUSR = 0;
	wdt_disable();

	// eliminate div-by-8 (no-op if 'div by 8' clock fuse not programmed)
	CLKPR = bit(CLKPCE);
	CLKPR = 0;

	// disable analog comparator -- saves power
	ACSRA = bit(ACD);

	util_init();
	init_timer();
	suart_init();
	init_debug();

	ir_init();
}

int main()
{
	init_led();
	blinky();

	hardware_setup();
	sei();

	if (do_debug()) do_debug_out();   // no return

	wdt_enable(WDTO_4S);


	while (1) {
		wdt_reset();
#ifndef NO_MONITOR
		monitor();
#endif
		led_handle();
		ir_process();

		if (ir_avail()) {
		    puthex(get_ir());
		    crnl();
		}
	}

}
