MAN1_TXT=$(wildcard git-*.txt)
MAN7_TXT=git.txt

DOC_HTML=$(patsubst %.txt,%.html,$(MAN1_TXT) $(MAN7_TXT))
DOC_HTML += glossary.html
DOC_HTML += tutorial.html
DOC_MAN1=$(patsubst %.txt,%.1,$(MAN1_TXT))
DOC_MAN7=$(patsubst %.txt,%.7,$(MAN7_TXT))

prefix=$(HOME)
bin=$(prefix)/bin
mandir=$(prefix)/man
man1=$(mandir)/man1
man7=$(mandir)/man7
# DESTDIR=

INSTALL=install

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


man: man1 man7
man1: $(DOC_MAN1)
man7: $(DOC_MAN7)

install:
	$(INSTALL) -m755 -d $(DESTDIR)/$(man1) $(DESTDIR)/$(man7)
	$(INSTALL) $(DOC_MAN1) $(DESTDIR)/$(man1)
	$(INSTALL) $(DOC_MAN7) $(DESTDIR)/$(man7)

# 'include' dependencies
git-diff-%.txt: diff-format.txt diff-options.txt
	touch $@

clean:
	rm -f *.xml *.html *.1 *.7 howto-index.txt

%.html : %.txt
	asciidoc -b xhtml11 -d manpage $<

%.1 %.7 : %.xml
	xmlto man $<

%.xml : %.txt
	asciidoc -b docbook -d manpage $<

glossary.html : glossary.txt sort_glossary.pl
	cat $< | \
	perl sort_glossary.pl | \
	asciidoc -b xhtml11 - > glossary.html

tutorial.html : tutorial.txt
	asciidoc -b xhtml11 tutorial.txt

howto-index.txt: howto-index.sh $(wildcard howto/*.txt)
	rm -f $@+ $@
	sh ./howto-index.sh $(wildcard howto/*.txt) >$@+
	mv $@+ $@

howto-index.html: howto-index.txt
	asciidoc -b xhtml11 howto-index.txt

WEBDOC_DEST = /pub/software/scm/git/docs

install-webdoc : html
	sh ./install-webdoc.sh $(WEBDOC_DEST)
