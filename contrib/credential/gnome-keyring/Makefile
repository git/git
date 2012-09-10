MAIN:=git-credential-gnome-keyring
all:: $(MAIN)

CC = gcc
RM = rm -f
CFLAGS = -g -O2 -Wall

-include ../../../config.mak.autogen
-include ../../../config.mak

INCS:=$(shell pkg-config --cflags gnome-keyring-1)
LIBS:=$(shell pkg-config --libs gnome-keyring-1)

SRCS:=$(MAIN).c
OBJS:=$(SRCS:.c=.o)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(INCS) -o $@ -c $<

$(MAIN): $(OBJS)
	$(CC) -o $@ $(LDFLAGS) $^ $(LIBS)

clean:
	@$(RM) $(MAIN) $(OBJS)
