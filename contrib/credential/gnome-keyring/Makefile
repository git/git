MAIN:=git-credential-gnome-keyring
all:: $(MAIN)

CC = gcc
RM = rm -f
CFLAGS = -g -O2 -Wall
PKG_CONFIG = pkg-config

-include ../../../config.mak.autogen
-include ../../../config.mak

INCS:=$(shell $(PKG_CONFIG) --cflags gnome-keyring-1 glib-2.0)
LIBS:=$(shell $(PKG_CONFIG) --libs gnome-keyring-1 glib-2.0)

SRCS:=$(MAIN).c
OBJS:=$(SRCS:.c=.o)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(INCS) -o $@ -c $<

$(MAIN): $(OBJS)
	$(CC) -o $@ $(LDFLAGS) $^ $(LIBS)

clean:
	@$(RM) $(MAIN) $(OBJS)
