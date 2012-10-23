all: git-credential-wincred.exe

CC = gcc
RM = rm -f
CFLAGS = -O2 -Wall

-include ../../../config.mak.autogen
-include ../../../config.mak

prefix ?= /usr/local
libexecdir ?= $(prefix)/libexec/git-core

INSTALL ?= install

git-credential-wincred.exe : git-credential-wincred.c
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) -o $@

install: git-credential-wincred.exe
	$(INSTALL) -m 755 $^ $(libexecdir)

clean:
	$(RM) git-credential-wincred.exe
