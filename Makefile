CC=gcc
CFLAGS=-Wall -O2
HOME=$(shell echo $$HOME)

PROGRAMS=mailsplit mailinfo
SCRIPTS=

all: $(PROGRAMS)

install: $(PROGRAMS) $(SCRIPTS)
	cp -f $(PROGRAMS) $(SCRIPTS) $(HOME)/bin/

clean:
	rm -f $(PROGRAMS) *.o
