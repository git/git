# The default target of this Makefile is...
all::

-include ../../../config.mak.autogen
-include ../../../config.mak

prefix ?= /usr/local
gitexecdir ?= $(prefix)/libexec/git-core

CC ?= gcc
CFLAGS ?= -g -O2 -Wall
PKG_CONFIG ?= pkg-config
INSTALL ?= install
RM ?= rm -f

MAIN:=git-credential-libsecret
all:: $(MAIN)

INCS:=$(shell $(PKG_CONFIG) --cflags libsecret-1 glib-2.0)
LIBS:=$(shell $(PKG_CONFIG) --libs libsecret-1 glib-2.0)

SRCS:=$(MAIN).c
OBJS:=$(SRCS:.c=.o)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(INCS) -o $@ -c $<

$(MAIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

install: $(MAIN)
	$(INSTALL) -d -m 755 $(DESTDIR)$(gitexecdir)
	$(INSTALL) -m 755 $< $(DESTDIR)$(gitexecdir)

clean:
	$(RM) $(MAIN) $(OBJS)

.PHONY: all install clean
