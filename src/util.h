/*
 * Copyright (c) 2013 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file
 * for details.
 */

#include "suart.h"   // for putch()/getch()

void puthex(unsigned char i);
void puthex16(unsigned int i);
void puthex32(long l);
void putdec16(unsigned int i);

void putstr(const char *s);
#if ALL_STRINGS_PROGMEM
#define putstr_p(s) putstr(s)
#else
void putstr_p(const prog_char * s);
#endif

void do_debug_out(void);

/* LED control */
# define DDRLED DDRB
# define PORTLED PORTB
# define PINLED PINB
# define BITLED PB0

#define led1_on()       do { PORTLED |=  bit(BITLED); } while(0)
#define led1_off()      do { PORTLED &= ~bit(BITLED); } while(0)
#define led1_flip()     do { PINLED   =  bit(BITLED); } while(0)
#define led1_is_on()       ( PINLED   &  bit(BITLED) )
void init_led(void);
void led_handle(void);
void led_flash(void);
void blinky(void);

/* tone control */
# define DDRTONE DDRA
# define PORTTONE PORTA
# define PINTONE PINA
# define TONEBITS (bit(PA5)|bit(PA6))  // we drive the speaker from two pins
# define ONE_TONEBIT bit(PA5)

#define tone_flip()     do { PINTONE =  TONEBITS; } while(0)  // toggle both
#define tone_cycle()    do { \
        if (tone_on && (tonecnt++ & tone_on) == 0) tone_flip(); } while(0)

void tone_hw_disable(void);
void tone_hw_enable(void);
void init_tone(void);
void tone_handle(void);
extern char tone_on, tonecnt;
void tone_start(char hilo, int duration);
// this is a little ugly, but a tone is described by both its
// nature and its length, so we put both tone_start() parameters in
// one #define.
#define TONE_CHIRP 1,75        // high, short
#define TONE_CONFIRM 1,200      // high, longer
#define TONE_ABORT 9,300        // low, even longer

void util_init(void);

// these macros output both names and values.  i.e.,
//  p_hex(foo)  results in the output "foo = 0x1234"
#define p_hex32(n) do { putstr(#n " = 0x"); puthex32(n); putstr("  "); } while(0)
#define p_hex(n) do { putstr(#n " = 0x"); puthex16(n); putstr("  "); } while(0)
#define p_dec(n) do { putstr(#n " = ");   putdec16(n); putstr("  "); } while(0)
#define p_str(s) do { putstr(#s " = '");  putstr(s);   putstr("' "); } while(0)

#define crnl()   putch('\n');


// vile:noti:sw=4
