PROGRAMS = vcardfilter
default: $(PROGRAMS)

LOCALVERSION	:= $(shell ./getlocalversion .)

CFLAGS	= -Wall

-include config.mk

CPPFLAGS+= -DVERSION="\"$(LOCALVERSION)\""

clean:
	rm -f $(wildcard *.o) $(PROGRAMS)
