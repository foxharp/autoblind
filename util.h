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

void putstr_p(const prog_char * s);
#if ! ALL_STRINGS_PROGMEM
void putstr(const char *s);
#else
#define putstr(s) putstr_p(s)
#endif
void init_led(void);
void led_handle(void);
void led_flash(void);

void blinky(void);

/* ground this i/o pin to enable early debug mode */
#define PORTDEBUG PORTB
#define PINDEBUG PINB
#define PDEBUG PB2
#define init_debug() {PORTDEBUG |= bit(PDEBUG);} // enable pullup
#define do_debug()  ((PINDEBUG & bit(PDEBUG)) == 0)
void do_debug_out(void);


/* LED control */
# define DDRLED DDRB
# define PORTLED PORTB
# define PINLED PINB
# define BITLED PB0

#define Led1_On()       do { PORTLED |=  bit(BITLED); } while(0)
#define Led1_Off()      do { PORTLED &= ~bit(BITLED); } while(0)
#define Led1_Flip()     do { PINLED   =  bit(BITLED); } while(0)
#define Led1_is_On()       ( PINLED   &  bit(BITLED) )

/* tone control */
# define DDRTONE DDRB
# define PORTTONE PORTB
# define PINTONE PINB
# define BITTONE PB0

#define Tone_On()       do { PORTTONE |=  bit(BITTONE); } while(0)
#define Tone_Off()      do { PORTTONE &= ~bit(BITTONE); } while(0)
#define Tone_Flip()     do { PINTONE   =  bit(BITTONE); } while(0)
#define Tone_is_On()       ( PINTONE  &   bit(BITTONE) )
#define tone_cycle()    do { if (tone_on) Tone_Flip(); } while(0)
extern char tone_on;
void tone_start(int duration);
#define TONE_CHIRP 100
#define TONE_CONFIRM 300

void util_init(void);

// these output both names and values.  i.e.,
// p_hex(foo)  results in  "foo = 0x1234"
#define p_hex32(n) do { putstr(#n " = 0x"); puthex32(n); putstr("  "); } while(0)
#define p_hex(n) do { putstr(#n " = 0x"); puthex16(n); putstr("  "); } while(0)
#define p_dec(n) do { putstr(#n " = ");   putdec16(n); putstr("  "); } while(0)
#define p_str(s) do { putstr(#s " = '");  putstr(s);   putstr("' "); } while(0)
#define crnl()   putch('\n');



// vile:noti:sw=4
