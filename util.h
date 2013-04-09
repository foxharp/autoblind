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

#define led1_on()       do { PORTLED |=  bit(BITLED); } while(0)
#define led1_off()      do { PORTLED &= ~bit(BITLED); } while(0)
#define led1_flip()     do { PINLED   =  bit(BITLED); } while(0)
#define led1_is_on()       ( PINLED   &  bit(BITLED) )
void init_led(void);
void led_handle(void);
void led_flash(void);
void blinky(void);

/* tone control */
/* drive the speaker push/pull from two pins */
# define DDRTONE DDRA
# define PORTTONE PORTA
# define PINTONE PINA
# define ONE_TONEBIT bit(PA5)
# define TONEBITS (bit(PA5)|bit(PA6))

#define tone_flip()     do { PINTONE =  TONEBITS; } while(0)  // toggle both
#define tone_cycle()    do { \
        if (tone_on && (tonecnt++ & tone_on) == 0) tone_flip(); } while(0)

void tone_hw_disable(void);
void tone_hw_enable(void);
void init_tone(void);
void tone_handle(void);
extern char tone_on, tonecnt;
void tone_start(char hilo, int duration);
#define TONE_CHIRP 1,100
#define TONE_CONFIRM 3,300

void util_init(void);

// these output both names and values.  i.e.,
// p_hex(foo)  results in  "foo = 0x1234"
#define p_hex32(n) do { putstr(#n " = 0x"); puthex32(n); putstr("  "); } while(0)
#define p_hex(n) do { putstr(#n " = 0x"); puthex16(n); putstr("  "); } while(0)
#define p_dec(n) do { putstr(#n " = ");   putdec16(n); putstr("  "); } while(0)
#define p_str(s) do { putstr(#s " = '");  putstr(s);   putstr("' "); } while(0)
#define crnl()   putch('\n');



// vile:noti:sw=4
