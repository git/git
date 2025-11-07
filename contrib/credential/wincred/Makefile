# The default target of this Makefile is...
all:: git-credential-wincred.exe

-include ../../../config.mak.autogen
-include ../../../config.mak

prefix ?= /usr/local
gitexecdir ?= $(prefix)/libexec/git-core

CC ?= gcc
CFLAGS ?= -O2 -Wall
INSTALL ?= install
RM ?= rm -f

git-credential-wincred.exe: git-credential-wincred.c
	$(LINK.c) -o $@ $^ $(LDFLAGS) $(LDLIBS)

install: git-credential-wincred.exe
	$(INSTALL) -d -m 755 $(DESTDIR)$(gitexecdir)
	$(INSTALL) -m 755 $< $(DESTDIR)$(gitexecdir)

clean:
	$(RM) git-credential-wincred.exe

.PHONY: all install clean
