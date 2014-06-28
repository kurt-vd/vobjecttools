PROGRAMS = vcardfilter
default: $(PROGRAMS)

LOCALVERSION	:= $(shell ./getlocalversion .)

CFLAGS	= -Wall
CPPFLAGS= -D_GNU_SOURCE

-include config.mk

CPPFLAGS+= -DVERSION="\"$(LOCALVERSION)\""

vcardfilter: vcard.o

clean:
	rm -f $(wildcard *.o) $(PROGRAMS)
