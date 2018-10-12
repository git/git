# The default target of this Makefile is...
all::

-include ../../config.mak.autogen
-include ../../config.mak

prefix ?= /usr/local
gitexecdir ?= $(prefix)/libexec/git-core
mandir ?= $(prefix)/share/man
man1dir ?= $(mandir)/man1
htmldir ?= $(prefix)/share/doc/git-doc

../../GIT-VERSION-FILE: FORCE
	$(MAKE) -C ../../ GIT-VERSION-FILE

-include ../../GIT-VERSION-FILE

# this should be set to a 'standard' bsd-type install program
INSTALL  ?= install
RM       ?= rm -f

ASCIIDOC         = asciidoc
ASCIIDOC_CONF    = -f ../../Documentation/asciidoc.conf
ASCIIDOC_HTML    = xhtml11
ASCIIDOC_DOCBOOK = docbook
ASCIIDOC_EXTRA   =
XMLTO            = xmlto

ifdef USE_ASCIIDOCTOR
ASCIIDOC         = asciidoctor
ASCIIDOC_CONF    =
ASCIIDOC_HTML    = xhtml5
ASCIIDOC_DOCBOOK = docbook45
ASCIIDOC_EXTRA  += -I../../Documentation -rasciidoctor-extensions
ASCIIDOC_EXTRA  += -alitdd='&\#x2d;&\#x2d;'
endif

ifndef SHELL_PATH
	SHELL_PATH = /bin/sh
endif
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))

MANPAGE_XSL   = ../../Documentation/manpage-normal.xsl

GIT_SUBTREE_SH := git-subtree.sh
GIT_SUBTREE    := git-subtree

GIT_SUBTREE_DOC := git-subtree.1
GIT_SUBTREE_XML := git-subtree.xml
GIT_SUBTREE_TXT := git-subtree.txt
GIT_SUBTREE_HTML := git-subtree.html
GIT_SUBTREE_TEST := ../../git-subtree

all:: $(GIT_SUBTREE)

$(GIT_SUBTREE): $(GIT_SUBTREE_SH)
	sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' $< >$@
	chmod +x $@

doc: $(GIT_SUBTREE_DOC) $(GIT_SUBTREE_HTML)

man: $(GIT_SUBTREE_DOC)

html: $(GIT_SUBTREE_HTML)

install: $(GIT_SUBTREE)
	$(INSTALL) -d -m 755 $(DESTDIR)$(gitexecdir)
	$(INSTALL) -m 755 $(GIT_SUBTREE) $(DESTDIR)$(gitexecdir)

install-doc: install-man install-html

install-man: $(GIT_SUBTREE_DOC)
	$(INSTALL) -d -m 755 $(DESTDIR)$(man1dir)
	$(INSTALL) -m 644 $^ $(DESTDIR)$(man1dir)

install-html: $(GIT_SUBTREE_HTML)
	$(INSTALL) -d -m 755 $(DESTDIR)$(htmldir)
	$(INSTALL) -m 644 $^ $(DESTDIR)$(htmldir)

$(GIT_SUBTREE_DOC): $(GIT_SUBTREE_XML)
	$(XMLTO) -m $(MANPAGE_XSL) man $^

$(GIT_SUBTREE_XML): $(GIT_SUBTREE_TXT)
	$(ASCIIDOC) -b $(ASCIIDOC_DOCBOOK) -d manpage $(ASCIIDOC_CONF) \
		-agit_version=$(GIT_VERSION) $(ASCIIDOC_EXTRA) $^

$(GIT_SUBTREE_HTML): $(GIT_SUBTREE_TXT)
	$(ASCIIDOC) -b $(ASCIIDOC_HTML) -d manpage $(ASCIIDOC_CONF) \
		-agit_version=$(GIT_VERSION) $(ASCIIDOC_EXTRA) $^

$(GIT_SUBTREE_TEST): $(GIT_SUBTREE)
	cp $< $@

test: $(GIT_SUBTREE_TEST)
	$(MAKE) -C t/ test

clean:
	$(RM) $(GIT_SUBTREE)
	$(RM) *.xml *.html *.1

.PHONY: FORCE
