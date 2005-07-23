#
# Make Linus git-tools
#
CC=gcc
COPTS=-O2
CFLAGS=-g $(COPTS) -Wall
INSTALL=install
HOME=$(shell echo $$HOME)
prefix=$(HOME)
bin=$(prefix)/bin
# dest=

PROGRAMS=mailsplit mailinfo
SCRIPTS=applymbox applypatch

all: $(PROGRAMS)

install: $(PROGRAMS) $(SCRIPTS)
	$(INSTALL) -m755 -d $(dest)$(bin)
	$(INSTALL) $(PROGRAMS) $(SCRIPTS) $(dest)$(bin)

clean:
	rm -f $(PROGRAMS) *.o
