all:: git-credential-osxkeychain

CC = gcc
RM = rm -f
CFLAGS = -g -O2 -Wall

-include ../../../config.mak.autogen
-include ../../../config.mak

git-credential-osxkeychain: git-credential-osxkeychain.o
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) \
		-framework Security -framework CoreFoundation

git-credential-osxkeychain.o: git-credential-osxkeychain.c
	$(CC) -c $(CFLAGS) $<

clean:
	$(RM) git-credential-osxkeychain git-credential-osxkeychain.o
