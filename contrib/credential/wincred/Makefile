all: git-credential-wincred.exe

CC = gcc
RM = rm -f
CFLAGS = -O2 -Wall

-include ../../../config.mak.autogen
-include ../../../config.mak

git-credential-wincred.exe : git-credential-wincred.c
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) -o $@

clean:
	$(RM) git-credential-wincred.exe
