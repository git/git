all: git-svn

prefix?=$(HOME)
bindir=$(prefix)/bin
mandir=$(prefix)/man
man1=$(mandir)/man1
INSTALL?=install
doc_conf=../../Documentation/asciidoc.conf
-include ../../config.mak

git-svn: git-svn.perl
	cp $< $@
	chmod +x $@

install: all
	$(INSTALL) -d -m755 $(DESTDIR)$(bindir)
	$(INSTALL) git-svn $(DESTDIR)$(bindir)

install-doc: doc
	$(INSTALL) git-svn.1 $(DESTDIR)$(man1)

doc: git-svn.1
git-svn.1 : git-svn.xml
	xmlto man git-svn.xml
git-svn.xml : git-svn.txt
	asciidoc -b docbook -d manpage \
		-f ../../Documentation/asciidoc.conf $<
git-svn.html : git-svn.txt
	asciidoc -b xhtml11 -d manpage \
		-f ../../Documentation/asciidoc.conf $<
test: git-svn
	cd t && $(SHELL) ./t0000-contrib-git-svn.sh
	cd t && $(SHELL) ./t0001-contrib-git-svn-props.sh

clean:
	rm -f git-svn *.xml *.html *.1
