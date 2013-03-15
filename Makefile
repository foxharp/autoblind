
# Makefile for xostick 
# Paul Fox, March 2013

VERSION = $(shell date +%y%m%d-%H%M)


PROG = xostick
SRCS = main.c ir.c monitor.c util.c timer.c suart.c
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
	ln -s xostick ../xostick-$(VERSION)
	tar -C .. --dereference \
	    --exclude CVS \
	    --exclude oldfiles \
	    --exclude web \
	    --exclude '*.tar.gz' \
	    -czf ../xostick-$(VERSION).tar.gz xostick-$(VERSION)
	mv ../xostick-$(VERSION).tar.gz .
	rm -f ../xostick-$(VERSION)

program:
	sudo avrdude -c usbtiny -p t861 -U $(PROG).hex

clean:
	rm -f *.o *.flash *.flash.* *.out *.map *.lst *.lss
	
clobber: clean
	rm -f xostick.hex

