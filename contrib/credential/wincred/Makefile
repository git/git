all: but-credential-wincred.exe

-include ../../../config.mak.autogen
-include ../../../config.mak

CC ?= gcc
RM ?= rm -f
CFLAGS ?= -O2 -Wall

prefix ?= /usr/local
libexecdir ?= $(prefix)/libexec/but-core

INSTALL ?= install

but-credential-wincred.exe : but-credential-wincred.c
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) -o $@

install: but-credential-wincred.exe
	$(INSTALL) -m 755 $^ $(libexecdir)

clean:
	$(RM) but-credential-wincred.exe
