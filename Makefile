
# Makefile for pgf's automatic blind controller 
# Paul Fox, March 2013

VERSION = $(shell date +%y%m%d-%H%M)


PROG = autoblind
SRCS = main.c ir.c monitor.c util.c timer.c suart.c blind.c button.c
HEADERS = $(shell echo *.h)

OBJS = $(subst .c,.o,$(SRCS))

# CFLAGS = -DNO_MSTIMER   # millisecond timer
# CFLAGS = -DNO_MONITOR -DNO_RECEIVE   # uart reception
# CFLAGS = -DNO_RECEIVE   # uart reception
# CFLAGS = -DMINIMAL_MONITOR

# note: printf works, but costs 1500 bytes
# CFLAGS = -DUSE_PRINTF   

# current code assumes ATTiny861.
MCU = attiny861
# the device is shipped at 1Mhz.
# this setting assumes you change the fuse, or change the prescaler
# early on.
F_CPU = 8000000

# location of cross-compiler -- edit to suit
#CROSS = /opt/avr-gcc-070314/bin/avr-
CROSS = avr-

CC=$(CROSS)gcc
LD=$(CROSS)gcc
NM=$(CROSS)nm
SIZE=$(CROSS)size
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump

CFLAGS += -c -Os -Wwrite-strings -Wall -mmcu=$(MCU)
CFLAGS += -DF_CPU=$(F_CPU)UL
CFLAGS += -Wa,-adhlns=$(<:%.c=%.lst)
CFLAGS += -DPROGRAM_VERSION="\"$(PROG)-$(VERSION)\""
LFLAGS = -mmcu=$(MCU)

# NOTE!  the next two assignments cause a) all initialized data
# to live forever in flash (i.e., be read-only), and b) all
# strings will need to be printed using pgm_read_byte().  of
# course, this applies to all other initialized data as well:  so
# declaring "int foo = 45;" and then trying to access 'foo'
# normally won't work.
CFLAGS += -DALL_STRINGS_PROGMEM
LFLAGS += -T avr25.x.data_in_flash

HOSTCC = gcc

all: $(PROG).hex $(PROG).lss

# builds are quick, so just make all objects depend on all headers,
# rather than having to track every dependency.
$(OBJS): $(HEADERS) Makefile

$(PROG).out: $(OBJS)
#	output previous object size, if any
	@-test -f $(PROG).out && (echo size was: ; $(SIZE) $(PROG).out) || true
	$(LD) -o $@ $(LFLAGS) $(OBJS)
	$(NM) -n $@ >$(PROG).map
	@echo current size is:
	@$(SIZE) $(PROG).out

$(PROG).hex: $(PROG).out
	$(OBJCOPY) -R .eeprom -O ihex $^ $@

# Create extended listing file from ELF output file.
%.lss: %.out
	$(OBJDUMP) -h -S $< > $@


sizes: $(OBJS)
	@echo
	@echo Individual:
	$(SIZE) -t $(OBJS)
	@echo
	@echo Complete:
	$(SIZE) $(PROG).out

tarball: all clean
	mkdir -p oldfiles
	mv $(PROG)-*.hex *.tar.gz oldfiles || true
	mv $(PROG).hex $(PROG)-$(VERSION).hex || true
	ln -s $(PROG) ../$(PROG)-$(VERSION)
	tar -C .. --dereference \
	    --exclude CVS \
	    --exclude oldfiles \
	    --exclude web \
	    --exclude '*.tar.gz' \
	    -czf ../$(PROG)-$(VERSION).tar.gz $(PROG)-$(VERSION)
	mv ../$(PROG)-$(VERSION).tar.gz .
	rm -f ../$(PROG)-$(VERSION)

program:
	sudo avrdude -c usbtiny -p t861 -U $(PROG).hex

# tip o' the hat to:
# http://www.frank-zhao.com/fusecalc/fusecalc.php?chip=attiny861&LOW=62&HIGH=DC&EXTENDED=FF&LOCKBIT=FF
bod_fuse:
	 sudo avrdude -c usbtiny -p t861 -U hfuse:w:0xDC:m

clean:
	rm -f *.o *.flash *.flash.* *.out *.map *.lst *.lss
	
clobber: clean
	rm -f $(PROG).hex

