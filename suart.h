/************************************************************************/
/*                                                                      */
/*                      Software UART using T1                          */
/*                                                                      */
/*              Author: P. Dannegger                                    */
/*                      danni@specs.de                                  */
/*                                                                      */
/************************************************************************/
/*
 * This file included in irmetermon by written permission of the
 * author.  irmetermon is licensed under GPL version 2, see accompanying
 * LICENSE file for details.
 */
#ifdef _AVR_IOM8_H_
# define SRX     PB0			// ICP on Mega8
# define SRXPIN  PINB

# define STX     PB1			// OC1A on Mega8
# define STXDDR  DDRB

# define STIFR	TIFR
# define STIMSK	TIMSK

# define TICIE1	ICIE1

#elif defined(_AVR_IOTN44_H_)

# define SRX     PA7			// ICP on tiny44
# define SRXPIN  PINA

# define STX     PA6			// OC1A on tiny44
# define STXDDR  DDRA

# define STIFR	TIFR1
# define STIMSK	TIMSK1

#elif defined(_AVR_IOTN861_H_)

#if RX_USE_INPUT_CAPTURE_INT
# define SRX     PA4			// ICP on tiny44
# define SRXPIN  PINA
#else
# define SRX     PB6			// INT0 on tiny44
# define SRXPIN  PINB
#endif

# define STX     PA6			// OC1A on tiny44
# define STXDDR  DDRA

# define STIFR	TIFR
# define STIMSK	TIMSK

#else
# error
# error Please add the defines:
# error SRX, SRXPIN, STX, STXDDR
# error for the new target !
# error
#endif

void putch(char c);
void putstr_p(const prog_char * s);
#if ! ALL_STRINGS_PROGMEM
void putstr(const char *s);
#else
#define putstr(s) putstr_p(PSTR(s))
#endif

#ifndef NO_RECEIVE
extern volatile unsigned char srx_done;
unsigned char getch(void);
#define kbhit()	(srx_done)		// true if byte received
#else
#define kbhit()	(0)			// never true
#endif

extern volatile unsigned char stx_count;
#define stx_active() (stx_count)

void suart_init(void);
// void putch( unsigned char val );
// void sputs_p( const prog_char *txt );
