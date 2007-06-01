MAN1_TXT= \
	$(filter-out $(addsuffix .txt, $(ARTICLES) $(SP_ARTICLES)), \
		$(wildcard git-*.txt)) \
	gitk.txt
MAN5_TXT=gitattributes.txt
MAN7_TXT=git.txt

DOC_HTML=$(patsubst %.txt,%.html,$(MAN1_TXT) $(MAN5_TXT) $(MAN7_TXT))

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

DOC_HTML += $(patsubst %,%.html,$(ARTICLES) $(SP_ARTICLES))

DOC_MAN1=$(patsubst %.txt,%.1,$(MAN1_TXT))
DOC_MAN5=$(patsubst %.txt,%.5,$(MAN5_TXT))
DOC_MAN7=$(patsubst %.txt,%.7,$(MAN7_TXT))

prefix?=$(HOME)
bindir?=$(prefix)/bin
mandir?=$(prefix)/man
man1dir=$(mandir)/man1
man5dir=$(mandir)/man5
man7dir=$(mandir)/man7
# DESTDIR=

ASCIIDOC=asciidoc
ASCIIDOC_EXTRA =
INSTALL?=install
DOC_REF = origin/man

-include ../config.mak.autogen
-include ../config.mak

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

install: man
	$(INSTALL) -d -m755 $(DESTDIR)$(man1dir)
	$(INSTALL) -d -m755 $(DESTDIR)$(man5dir)
	$(INSTALL) -d -m755 $(DESTDIR)$(man7dir)
	$(INSTALL) -m644 $(DOC_MAN1) $(DESTDIR)$(man1dir)
	$(INSTALL) -m644 $(DOC_MAN5) $(DESTDIR)$(man5dir)
	$(INSTALL) -m644 $(DOC_MAN7) $(DESTDIR)$(man7dir)


../GIT-VERSION-FILE: .FORCE-GIT-VERSION-FILE
	$(MAKE) -C ../ GIT-VERSION-FILE

-include ../GIT-VERSION-FILE

#
# Determine "include::" file references in asciidoc files.
#
doc.dep : $(wildcard *.txt) build-docdep.perl
	rm -f $@+ $@
	perl ./build-docdep.perl >$@+
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

cmd-list.made: cmd-list.perl $(MAN1_TXT)
	perl ./cmd-list.perl
	date >$@

git.7 git.html: git.txt core-intro.txt

clean:
	rm -f *.xml *.xml+ *.html *.html+ *.1 *.5 *.7 howto-index.txt howto/*.html doc.dep
	rm -f $(cmds_txt) *.made

%.html : %.txt
	rm -f $@+ $@
	$(ASCIIDOC) -b xhtml11 -d manpage -f asciidoc.conf \
		$(ASCIIDOC_EXTRA) -agit_version=$(GIT_VERSION) -o $@+ $<
	mv $@+ $@

%.1 %.5 %.7 : %.xml
	xmlto -m callouts.xsl man $<

%.xml : %.txt
	rm -f $@+ $@
	$(ASCIIDOC) -b docbook -d manpage -f asciidoc.conf \
		$(ASCIIDOC_EXTRA) -agit_version=$(GIT_VERSION) -o $@+ $<
	mv $@+ $@

user-manual.xml: user-manual.txt user-manual.conf
	$(ASCIIDOC) -b docbook -d book $<

XSLT = http://docbook.sourceforge.net/release/xsl/current/html/docbook.xsl
XSLTOPTS = --xinclude --stringparam html.stylesheet docbook-xsl.css

user-manual.html: user-manual.xml
	xsltproc $(XSLTOPTS) -o $@ $(XSLT) $<

howto-index.txt: howto-index.sh $(wildcard howto/*.txt)
	rm -f $@+ $@
	sh ./howto-index.sh $(wildcard howto/*.txt) >$@+
	mv $@+ $@

$(patsubst %,%.html,$(ARTICLES)) : %.html : %.txt
	$(ASCIIDOC) -b xhtml11 $*.txt

WEBDOC_DEST = /pub/software/scm/git/docs

$(patsubst %.txt,%.html,$(wildcard howto/*.txt)): %.html : %.txt
	rm -f $@+ $@
	sed -e '1,/^$$/d' $< | $(ASCIIDOC) -b xhtml11 - >$@+
	mv $@+ $@

install-webdoc : html
	sh ./install-webdoc.sh $(WEBDOC_DEST)

quick-install:
	sh ./install-doc-quick.sh $(DOC_REF) $(mandir)

.PHONY: .FORCE-GIT-VERSION-FILE
