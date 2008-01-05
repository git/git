MAN1_TXT= \
	$(filter-out $(addsuffix .txt, $(ARTICLES) $(SP_ARTICLES)), \
		$(wildcard git-*.txt)) \
	gitk.txt
MAN5_TXT=gitattributes.txt gitignore.txt gitcli.txt gitmodules.txt
MAN7_TXT=git.txt

MAN_TXT = $(MAN1_TXT) $(MAN5_TXT) $(MAN7_TXT)
MAN_XML=$(patsubst %.txt,%.xml,$(MAN_TXT))
MAN_HTML=$(patsubst %.txt,%.html,$(MAN_TXT))

DOC_HTML=$(MAN_HTML)

ARTICLES = tutorial
ARTICLES += tutorial-2
ARTICLES += core-tutorial
ARTICLES += cvs-migration
ARTICLES += diffcore
ARTICLES += howto-index
ARTICLES += repository-layout
ARTICLES += hooks
ARTICLES += everyday
ARTICLES += git-tools
ARTICLES += glossary
# with their own formatting rules.
SP_ARTICLES = howto/revert-branch-rebase user-manual
API_DOCS = $(patsubst %.txt,%,$(filter-out technical/api-index-skel.txt technical/api-index.txt, $(wildcard technical/api-*.txt)))
SP_ARTICLES += $(API_DOCS)
SP_ARTICLES += technical/api-index

DOC_HTML += $(patsubst %,%.html,$(ARTICLES) $(SP_ARTICLES))

DOC_MAN1=$(patsubst %.txt,%.1,$(MAN1_TXT))
DOC_MAN5=$(patsubst %.txt,%.5,$(MAN5_TXT))
DOC_MAN7=$(patsubst %.txt,%.7,$(MAN7_TXT))

prefix?=$(HOME)
bindir?=$(prefix)/bin
htmldir?=$(prefix)/share/doc/git-doc
mandir?=$(prefix)/share/man
man1dir=$(mandir)/man1
man5dir=$(mandir)/man5
man7dir=$(mandir)/man7
# DESTDIR=

ASCIIDOC=asciidoc
ASCIIDOC_EXTRA =
INSTALL?=install
RM ?= rm -f
DOC_REF = origin/man

infodir?=$(prefix)/share/info
MAKEINFO=makeinfo
INSTALL_INFO=install-info
DOCBOOK2X_TEXI=docbook2x-texi
ifndef PERL_PATH
	PERL_PATH = /usr/bin/perl
endif

-include ../config.mak.autogen
-include ../config.mak

ifdef ASCIIDOC8
ASCIIDOC_EXTRA += -a asciidoc7compatible
endif
ifdef DOCBOOK_XSL_172
ASCIIDOC_EXTRA += -a docbook-xsl-172
endif

#
# Please note that there is a minor bug in asciidoc.
# The version after 6.0.3 _will_ include the patch found here:
#   http://marc.theaimsgroup.com/?l=git&m=111558757202243&w=2
#
# Until that version is released you may have to apply the patch
# yourself - yes, all 6 characters of it!
#

all: html man

html: $(DOC_HTML)

$(DOC_HTML) $(DOC_MAN1) $(DOC_MAN5) $(DOC_MAN7): asciidoc.conf

man: man1 man5 man7
man1: $(DOC_MAN1)
man5: $(DOC_MAN5)
man7: $(DOC_MAN7)

info: git.info gitman.info

install: man
	$(INSTALL) -d -m 755 $(DESTDIR)$(man1dir)
	$(INSTALL) -d -m 755 $(DESTDIR)$(man5dir)
	$(INSTALL) -d -m 755 $(DESTDIR)$(man7dir)
	$(INSTALL) -m 644 $(DOC_MAN1) $(DESTDIR)$(man1dir)
	$(INSTALL) -m 644 $(DOC_MAN5) $(DESTDIR)$(man5dir)
	$(INSTALL) -m 644 $(DOC_MAN7) $(DESTDIR)$(man7dir)

install-info: info
	$(INSTALL) -d -m 755 $(DESTDIR)$(infodir)
	$(INSTALL) -m 644 git.info gitman.info $(DESTDIR)$(infodir)
	if test -r $(DESTDIR)$(infodir)/dir; then \
	  $(INSTALL_INFO) --info-dir=$(DESTDIR)$(infodir) git.info ;\
	  $(INSTALL_INFO) --info-dir=$(DESTDIR)$(infodir) gitman.info ;\
	else \
	  echo "No directory found in $(DESTDIR)$(infodir)" >&2 ; \
	fi

install-html: html
	sh ./install-webdoc.sh $(DESTDIR)$(htmldir)

../GIT-VERSION-FILE: .FORCE-GIT-VERSION-FILE
	$(MAKE) -C ../ GIT-VERSION-FILE

-include ../GIT-VERSION-FILE

#
# Determine "include::" file references in asciidoc files.
#
doc.dep : $(wildcard *.txt) build-docdep.perl
	$(RM) $@+ $@
	$(PERL_PATH) ./build-docdep.perl >$@+
	mv $@+ $@

-include doc.dep

cmds_txt = cmds-ancillaryinterrogators.txt \
	cmds-ancillarymanipulators.txt \
	cmds-mainporcelain.txt \
	cmds-plumbinginterrogators.txt \
	cmds-plumbingmanipulators.txt \
	cmds-synchingrepositories.txt \
	cmds-synchelpers.txt \
	cmds-purehelpers.txt \
	cmds-foreignscminterface.txt

$(cmds_txt): cmd-list.made

cmd-list.made: cmd-list.perl ../command-list.txt $(MAN1_TXT)
	$(RM) $@
	$(PERL_PATH) ./cmd-list.perl ../command-list.txt
	date >$@

git.7 git.html: git.txt

clean:
	$(RM) *.xml *.xml+ *.html *.html+ *.1 *.5 *.7 *.texi *.texi+ git.info
	$(RM) howto-index.txt howto/*.html doc.dep
	$(RM) technical/api-*.html technical/api-index.txt
	$(RM) $(cmds_txt) *.made

$(MAN_HTML): %.html : %.txt
	$(RM) $@+ $@
	$(ASCIIDOC) -b xhtml11 -d manpage -f asciidoc.conf \
		$(ASCIIDOC_EXTRA) -agit_version=$(GIT_VERSION) -o $@+ $<
	mv $@+ $@

%.1 %.5 %.7 : %.xml
	$(RM) $@
	xmlto -m callouts.xsl man $<

%.xml : %.txt
	$(RM) $@+ $@
	$(ASCIIDOC) -b docbook -d manpage -f asciidoc.conf \
		$(ASCIIDOC_EXTRA) -agit_version=$(GIT_VERSION) -o $@+ $<
	mv $@+ $@

user-manual.xml: user-manual.txt user-manual.conf
	$(ASCIIDOC) -b docbook -d book $<

technical/api-index.txt: technical/api-index-skel.txt \
	technical/api-index.sh $(patsubst %,%.txt,$(API_DOCS))
	cd technical && sh ./api-index.sh

$(patsubst %,%.html,$(API_DOCS) technical/api-index): %.html : %.txt
	$(ASCIIDOC) -b xhtml11 -f asciidoc.conf \
		$(ASCIIDOC_EXTRA) -agit_version=$(GIT_VERSION) $*.txt

XSLT = docbook.xsl
XSLTOPTS = --xinclude --stringparam html.stylesheet docbook-xsl.css

user-manual.html: user-manual.xml
	xsltproc $(XSLTOPTS) -o $@ $(XSLT) $<

git.info: user-manual.texi
	$(MAKEINFO) --no-split -o $@ user-manual.texi

user-manual.texi: user-manual.xml
	$(RM) $@+ $@
	$(DOCBOOK2X_TEXI) user-manual.xml --to-stdout | $(PERL_PATH) fix-texi.perl >$@+
	mv $@+ $@

gitman.texi: $(MAN_XML) cat-texi.perl
	$(RM) $@+ $@
	($(foreach xml,$(MAN_XML),$(DOCBOOK2X_TEXI) --to-stdout $(xml);)) | \
	$(PERL_PATH) cat-texi.perl $@ >$@+
	mv $@+ $@

gitman.info: gitman.texi
	$(MAKEINFO) --no-split $*.texi

$(patsubst %.txt,%.texi,$(MAN_TXT)): %.texi : %.xml
	$(RM) $@+ $@
	$(DOCBOOK2X_TEXI) --to-stdout $*.xml >$@+
	mv $@+ $@

howto-index.txt: howto-index.sh $(wildcard howto/*.txt)
	$(RM) $@+ $@
	sh ./howto-index.sh $(wildcard howto/*.txt) >$@+
	mv $@+ $@

$(patsubst %,%.html,$(ARTICLES)) : %.html : %.txt
	$(ASCIIDOC) -b xhtml11 $*.txt

WEBDOC_DEST = /pub/software/scm/git/docs

$(patsubst %.txt,%.html,$(wildcard howto/*.txt)): %.html : %.txt
	$(RM) $@+ $@
	sed -e '1,/^$$/d' $< | $(ASCIIDOC) -b xhtml11 - >$@+
	mv $@+ $@

install-webdoc : html
	sh ./install-webdoc.sh $(WEBDOC_DEST)

quick-install:
	sh ./install-doc-quick.sh $(DOC_REF) $(DESTDIR)$(mandir)

.PHONY: .FORCE-GIT-VERSION-FILE
