#
# Make Linus git-tools
#
CC=gcc
CFLAGS = -O2 -g -Wall
ALL_CFLAGS = $(CFLAGS)
INSTALL=install
prefix=$(HOME)
bindir=$(prefix)/bin
# DESTDIR=

PROGRAMS=git-mailsplit git-mailinfo
SCRIPTS=git-applymbox git-applypatch

git-%: %.c
	$(CC) $(ALL_CFLAGS) -o $@ $(filter %.c,$^)

all: $(PROGRAMS)

install: $(PROGRAMS) $(SCRIPTS)
	$(INSTALL) -m755 -d $(dest)$(bindir)
	$(INSTALL) $(PROGRAMS) $(SCRIPTS) $(DESTDIR)$(bindir)

clean:
	rm -f $(PROGRAMS) *.o
