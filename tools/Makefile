#
# Make Linus git-tools
#
CC=gcc
COPTS=-O2
CFLAGS=-g $(COPTS) -Wall
INSTALL=install
HOME=$(shell echo $$HOME)
prefix=$(HOME)
bindir=$(prefix)/bin
# dest=

PROGRAMS=git-mailsplit git-mailinfo
SCRIPTS=git-applymbox git-applypatch

git-%: %.c
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

all: $(PROGRAMS)

install: $(PROGRAMS) $(SCRIPTS)
	$(INSTALL) -m755 -d $(dest)$(bindir)
	$(INSTALL) $(PROGRAMS) $(SCRIPTS) $(dest)$(bindir)

clean:
	rm -f $(PROGRAMS) *.o
