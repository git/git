# The default target of this Makefile is...
all:: git-credential-osxkeychain

-include ../../../config.mak.autogen
-include ../../../config.mak

prefix ?= /usr/local
gitexecdir ?= $(prefix)/libexec/git-core

CC ?= gcc
CFLAGS ?= -g -O2 -Wall
RM ?= rm -f

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ -c $<

git-credential-osxkeychain: git-credential-osxkeychain.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) \
		-framework Security -framework CoreFoundation

clean:
	$(RM) git-credential-osxkeychain git-credential-osxkeychain.o

.PHONY: all clean
