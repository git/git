#
# Copyright (C) 2013
#     Matthieu Moy <Matthieu.Moy@imag.fr>
#
# To build and test:
#
#   make
#   bin-wrapper/but mw preview Some_page.mw
#   bin-wrapper/but clone mediawiki::http://example.com/wiki/
#
# To install, run Git's toplevel 'make install' then run:
#
#   make install

BUT_MEDIAWIKI_PM=Git/Mediawiki.pm
SCRIPT_PERL=but-remote-mediawiki.perl
SCRIPT_PERL+=but-mw.perl
BUT_ROOT_DIR=../..
HERE=contrib/mw-to-but/

INSTALL = install

SCRIPT_PERL_FULL=$(patsubst %,$(HERE)/%,$(SCRIPT_PERL))
INSTLIBDIR=$(shell $(MAKE) -C $(BUT_ROOT_DIR)/ \
                -s --no-print-directory prefix=$(prefix) \
                perllibdir=$(perllibdir) perllibdir)
DESTDIR_SQ = $(subst ','\'',$(DESTDIR))
INSTLIBDIR_SQ = $(subst ','\'',$(INSTLIBDIR))

all: build

test: all
	$(MAKE) -C t

check: perlcritic test

install_pm:
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(INSTLIBDIR_SQ)/Git'
	$(INSTALL) -m 644 $(BUT_MEDIAWIKI_PM) \
		'$(DESTDIR_SQ)$(INSTLIBDIR_SQ)/$(BUT_MEDIAWIKI_PM)'

build:
	$(MAKE) -C $(BUT_ROOT_DIR) SCRIPT_PERL="$(SCRIPT_PERL_FULL)" \
                build-perl-script

install: install_pm
	$(MAKE) -C $(BUT_ROOT_DIR) SCRIPT_PERL="$(SCRIPT_PERL_FULL)" \
                install-perl-script

clean:
	$(MAKE) -C $(BUT_ROOT_DIR) SCRIPT_PERL="$(SCRIPT_PERL_FULL)" \
                clean-perl-script

perlcritic:
	perlcritic -5 $(SCRIPT_PERL)
	-perlcritic -2 $(SCRIPT_PERL)

.PHONY: all test check install_pm install clean perlcritic
