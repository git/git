# The default target of this Makefile is...
all::

-include ../../../config.mak.autogen
-include ../../../config.mak

prefix ?= /usr/local
gitexecdir ?= $(prefix)/libexec/git-core

CC ?= gcc
CFLAGS ?= -g -O2 -Wall
INSTALL ?= install
RM ?= rm -f

MAIN:=git-credential-osxkeychain
all:: $(MAIN)

SRCS:=$(MAIN).c
OBJS:=$(SRCS:.c=.o)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ -c $<

$(MAIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) \
		-framework Security -framework CoreFoundation

install: $(MAIN)
	$(INSTALL) -d -m 755 $(DESTDIR)$(gitexecdir)
	$(INSTALL) -m 755 $< $(DESTDIR)$(gitexecdir)

clean:
	$(RM) $(MAIN) $(OBJS)

.PHONY: all install clean
