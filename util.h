/* vi: set sw=4 ts=4: */
/*
 * Copyright (c) 2012 Paul Fox, pgf@foxharp.boston.ma.us
 *
 * Licensed under GPL version 2, see accompanying LICENSE file
 * for details.
 */
void puthex(unsigned char i);
void puthex16(unsigned int i);
void puthex32(long l);

void putdec16(unsigned int i);

void putstr_p(const prog_char * s);
#if ! ALL_STRINGS_PROGMEM
void putstr(const char *s);
#else
#define putstr(s) putstr_p(PSTR(s))
#endif
void init_led(void);
void led_handle(void);
void led_flash(void);

void util_init(void);

#define p_hex(n) do { putstr(#n " = 0x"); puthex16(n); putstr(" ");  } while(0)
#define p_dec(n) do { putstr(#n " = ");   putdec16(n); putstr(" ");  } while(0)
#define p_str(s) do { putstr(#s " = '");  putstr(s);   putstr("' "); } while(0)
#define crnl()	putch('\n');
