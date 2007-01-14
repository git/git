MAN1_TXT= \
	$(filter-out $(addsuffix .txt, $(ARTICLES) $(SP_ARTICLES)), \
		$(wildcard git-*.txt)) \
	gitk.txt
MAN7_TXT=git.txt

DOC_HTML=$(patsubst %.txt,%.html,$(MAN1_TXT) $(MAN7_TXT))

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
# with their own formatting rules.
SP_ARTICLES = glossary howto/revert-branch-rebase

DOC_HTML += $(patsubst %,%.html,$(ARTICLES) $(SP_ARTICLES))

DOC_MAN1=$(patsubst %.txt,%.1,$(MAN1_TXT))
DOC_MAN7=$(patsubst %.txt,%.7,$(MAN7_TXT))

prefix?=$(HOME)
bindir?=$(prefix)/bin
mandir?=$(prefix)/man
man1dir=$(mandir)/man1
man7dir=$(mandir)/man7
# DESTDIR=

INSTALL?=install
DOC_REF = origin/man

-include ../config.mak.autogen

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

$(DOC_HTML) $(DOC_MAN1) $(DOC_MAN7): asciidoc.conf

man: man1 man7
man1: $(DOC_MAN1)
man7: $(DOC_MAN7)

install: man
	$(INSTALL) -d -m755 $(DESTDIR)$(man1dir) $(DESTDIR)$(man7dir)
	$(INSTALL) -m644 $(DOC_MAN1) $(DESTDIR)$(man1dir)
	$(INSTALL) -m644 $(DOC_MAN7) $(DESTDIR)$(man7dir)


#
# Determine "include::" file references in asciidoc files.
#
doc.dep : $(wildcard *.txt) build-docdep.perl
	rm -f $@+ $@
	perl ./build-docdep.perl >$@+
	mv $@+ $@

-include doc.dep

git.7: README

README: ../README
	cp $< $@


clean:
	rm -f *.xml *.html *.1 *.7 howto-index.txt howto/*.html doc.dep README

%.html : %.txt
	asciidoc -b xhtml11 -d manpage -f asciidoc.conf $<

%.1 %.7 : %.xml
	xmlto -m callouts.xsl man $<

%.xml : %.txt
	asciidoc -b docbook -d manpage -f asciidoc.conf $<

git.html: git.txt README

glossary.html : glossary.txt sort_glossary.pl
	cat $< | \
	perl sort_glossary.pl | \
	asciidoc -b xhtml11 - > glossary.html

howto-index.txt: howto-index.sh $(wildcard howto/*.txt)
	rm -f $@+ $@
	sh ./howto-index.sh $(wildcard howto/*.txt) >$@+
	mv $@+ $@

$(patsubst %,%.html,$(ARTICLES)) : %.html : %.txt
	asciidoc -b xhtml11 $*.txt

WEBDOC_DEST = /pub/software/scm/git/docs

$(patsubst %.txt,%.html,$(wildcard howto/*.txt)): %.html : %.txt
	rm -f $@+ $@
	sed -e '1,/^$$/d' $< | asciidoc -b xhtml11 - >$@+
	mv $@+ $@

install-webdoc : html
	sh ./install-webdoc.sh $(WEBDOC_DEST)

quick-install:
	sh ./install-doc-quick.sh $(DOC_REF) $(mandir)
