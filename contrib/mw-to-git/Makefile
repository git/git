#
# Copyright (C) 2013
#     Matthieu Moy <Matthieu.Moy@imag.fr>
#
## Build git-remote-mediawiki

SCRIPT_PERL=git-remote-mediawiki.perl
GIT_ROOT_DIR=../..
HERE=contrib/mw-to-git/

SCRIPT_PERL_FULL=$(patsubst %,$(HERE)/%,$(SCRIPT_PERL))

all: build

build install clean:
	$(MAKE) -C $(GIT_ROOT_DIR) SCRIPT_PERL=$(SCRIPT_PERL_FULL) \
                $@-perl-script
