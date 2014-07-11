PROGRAMS = vcardquery testvcard
default: $(PROGRAMS)

LOCALVERSION	:= $(shell ./getlocalversion .)

PREFIX	= /usr/local
CFLAGS	= -Wall
CPPFLAGS= -D_GNU_SOURCE

-include config.mk

CPPFLAGS+= -DVERSION="\"$(LOCALVERSION)\""

vcardquery: vcard.o
testvcard: vcard.o

install: $(PROGRAMS)
	install -vs -t $(DESTDIR)$(PREFIX)/bin/ $(PROGRAMS)

clean:
	rm -f $(wildcard *.o) $(PROGRAMS)
