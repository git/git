all:: but-credential-osxkeychain

CC = gcc
RM = rm -f
CFLAGS = -g -O2 -Wall

-include ../../../config.mak.autogen
-include ../../../config.mak

but-credential-osxkeychain: but-credential-osxkeychain.o
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -Wl,-framework -Wl,Security

but-credential-osxkeychain.o: but-credential-osxkeychain.c
	$(CC) -c $(CFLAGS) $<

clean:
	$(RM) but-credential-osxkeychain but-credential-osxkeychain.o
