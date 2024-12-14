# Import tree-wide shared Makefile behavior and libraries
include ../shared.mak

# make and install sample templates
INSTALL ?= install
TAR ?= tar
RM ?= rm -f
prefix ?= $(HOME)
template_instdir ?= $(prefix)/share/git-core/templates
# DESTDIR=

ifndef SHELL_PATH
	SHELL_PATH = /bin/sh
endif
ifndef PERL_PATH
	PERL_PATH = perl
endif

SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))
PERL_PATH_SQ = $(subst ','\'',$(PERL_PATH))

# Shell quote (do not use $(call) to accommodate ancient setups);
DESTDIR_SQ = $(subst ','\'',$(DESTDIR))
template_instdir_SQ = $(subst ','\'',$(template_instdir))

all: boilerplates.made custom

# Put templates that can be copied straight from the source
# in a file direc--tory--file in the source.  They will be
# just copied to the destination.

TEMPLATES  =
TEMPLATES += description
TEMPLATES += hooks/applypatch-msg.sample
TEMPLATES += hooks/commit-msg.sample
TEMPLATES += hooks/fsmonitor-watchman.sample
TEMPLATES += hooks/post-update.sample
TEMPLATES += hooks/pre-applypatch.sample
TEMPLATES += hooks/pre-commit.sample
TEMPLATES += hooks/pre-merge-commit.sample
TEMPLATES += hooks/prepare-commit-msg.sample
TEMPLATES += hooks/pre-push.sample
TEMPLATES += hooks/pre-rebase.sample
TEMPLATES += hooks/pre-receive.sample
TEMPLATES += hooks/push-to-checkout.sample
TEMPLATES += hooks/sendemail-validate.sample
TEMPLATES += hooks/update.sample
TEMPLATES += info/exclude

boilerplates.made: $(TEMPLATES)
	$(QUIET)umask 022 && for template in $(TEMPLATES); \
	do \
		dir=$$(dirname "$$template") && \
		mkdir -p blt/$$dir && \
		sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
		    -e 's|@SHELL_PATH@|$(SHELL_PATH_SQ)|' \
		    -e 's|@PERL_PATH@|$(PERL_PATH_SQ)|g' $$template > \
			blt/$$template && \
		if test -x "$$template"; then rx=rx; else rx=r; fi && \
		chmod a+$$rx "blt/$$template" || exit; \
	done && \
	date >$@

# If you need build-tailored templates, build them into blt/
# directory yourself here.
custom:
	$(QUIET): no custom templates yet

clean:
	$(RM) -r blt boilerplates.made

install: all
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(template_instdir_SQ)'
	(cd blt && $(TAR) cf - .) | \
	(cd '$(DESTDIR_SQ)$(template_instdir_SQ)' && umask 022 && $(TAR) xof -)
