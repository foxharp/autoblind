/*
 * Copyright (c) 2013 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file
 * for details.
 */
#ifndef F_CPU
#error F_CPU must be defined
#endif

/* personal preference macros/typedefs */
typedef uint16_t word;
typedef uint8_t byte;
#define bit(x) _BV(x)

void monitor(void);

void force_reboot(void);

#ifdef USE_PRINTF
#include <stdio.h>
#endif

// vile:noti:sw=4
