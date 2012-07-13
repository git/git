#
# Copyright (C) 2012
#     Charles Roussel <charles.roussel@ensimag.imag.fr>
#     Simon Cathebras <simon.cathebras@ensimag.imag.fr>
#     Julien Khayat <julien.khayat@ensimag.imag.fr>
#     Guillaume Sasdy <guillaume.sasdy@ensimag.imag.fr>
#     Simon Perrat <simon.perrat@ensimag.imag.fr>
#
## Build git-remote-mediawiki

-include ../../config.mak.autogen
-include ../../config.mak

ifndef PERL_PATH
	PERL_PATH = /usr/bin/perl
endif
ifndef gitexecdir
	gitexecdir = $(shell git --exec-path)
endif

PERL_PATH_SQ = $(subst ','\'',$(PERL_PATH))
gitexecdir_SQ = $(subst ','\'',$(gitexecdir))
SCRIPT = git-remote-mediawiki

.PHONY: install help doc test clean

help:
	@echo 'This is the help target of the Makefile. Current configuration:'
	@echo '  gitexecdir = $(gitexecdir_SQ)'
	@echo '  PERL_PATH = $(PERL_PATH_SQ)'
	@echo 'Run "$(MAKE) install" to install $(SCRIPT) in gitexecdir'
	@echo 'Run "$(MAKE) test" to run the testsuite'

install:
	sed -e '1s|#!.*/perl|#!$(PERL_PATH_SQ)|' $(SCRIPT) \
		> '$(gitexecdir_SQ)/$(SCRIPT)'
	chmod +x '$(gitexecdir)/$(SCRIPT)'

doc:
	@echo 'Sorry, "make doc" is not implemented yet for $(SCRIPT)'

test:
	$(MAKE) -C t/ test

clean:
	$(RM) '$(gitexecdir)/$(SCRIPT)'
	$(MAKE) -C t/ clean
