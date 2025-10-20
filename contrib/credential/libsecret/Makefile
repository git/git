# The default target of this Makefile is...
all:: git-credential-libsecret

-include ../../../config.mak.autogen
-include ../../../config.mak

prefix ?= /usr/local
gitexecdir ?= $(prefix)/libexec/git-core

CC ?= gcc
CFLAGS ?= -g -O2 -Wall
PKG_CONFIG ?= pkg-config
RM ?= rm -f

INCS:=$(shell $(PKG_CONFIG) --cflags libsecret-1 glib-2.0)
LIBS:=$(shell $(PKG_CONFIG) --libs libsecret-1 glib-2.0)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(INCS) -o $@ -c $<

git-credential-libsecret: git-credential-libsecret.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

clean:
	$(RM) git-credential-libsecret git-credential-libsecret.o

.PHONY: all clean
