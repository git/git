all: git-svn

prefix?=$(HOME)
bindir?=$(prefix)/bin
mandir?=$(prefix)/man
man1dir=$(mandir)/man1
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
	$(INSTALL) git-svn.1 $(DESTDIR)$(man1dir)

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
	cd t && for i in t????-*.sh; do $(SHELL) ./$$i $(TEST_FLAGS); done

# we can test NO_OPTIMIZE_COMMITS independently of LC_ALL
full-test:
	$(MAKE) test GIT_SVN_NO_LIB=1 GIT_SVN_NO_OPTIMIZE_COMMITS=1 LC_ALL=C
	$(MAKE) test GIT_SVN_NO_LIB=0 GIT_SVN_NO_OPTIMIZE_COMMITS=1 LC_ALL=C
	$(MAKE) test GIT_SVN_NO_LIB=1 GIT_SVN_NO_OPTIMIZE_COMMITS=0 \
							LC_ALL=en_US.UTF-8
	$(MAKE) test GIT_SVN_NO_LIB=0 GIT_SVN_NO_OPTIMIZE_COMMITS=0 \
							LC_ALL=en_US.UTF-8

clean:
	rm -f git-svn *.xml *.html *.1
