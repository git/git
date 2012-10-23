-include ../../../config.mak.autogen
-include ../../../config.mak

prefix ?= /usr/local
libexecdir ?= $(prefix)/libexec/git-core

INSTALL ?= install

GIT_CREDENTIAL_WINCRED := git-credential-wincred.exe

all: $(GIT_CREDENTIAL_WINCRED)

$(GIT_CREDENTIAL_WINCRED): git-credential-wincred.c
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) -o $@

install: $(GIT_CREDENTIAL_WINCRED)
	$(INSTALL) -m 755 $(GIT_CREDENTIAL_WINCRED) $(libexecdir)

clean:
	$(RM) $(GIT_CREDENTIAL_WINCRED)
