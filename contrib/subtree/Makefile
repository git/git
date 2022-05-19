# The default target of this Makefile is...
all::

-include ../../config.mak.autogen
-include ../../config.mak

prefix ?= /usr/local
butexecdir ?= $(prefix)/libexec/but-core
mandir ?= $(prefix)/share/man
man1dir ?= $(mandir)/man1
htmldir ?= $(prefix)/share/doc/but-doc

../../BUT-VERSION-FILE: FORCE
	$(MAKE) -C ../../ BUT-VERSION-FILE

-include ../../BUT-VERSION-FILE

# this should be set to a 'standard' bsd-type install program
INSTALL  ?= install
RM       ?= rm -f

ASCIIDOC         = asciidoc
ASCIIDOC_CONF    = -f ../../Documentation/asciidoc.conf
ASCIIDOC_HTML    = xhtml11
ASCIIDOC_DOCBOOK = docbook
ASCIIDOC_EXTRA   =
XMLTO            = xmlto
XMLTO_EXTRA      =

ifdef USE_ASCIIDOCTOR
ASCIIDOC         = asciidoctor
ASCIIDOC_CONF    =
ASCIIDOC_HTML    = xhtml5
ASCIIDOC_DOCBOOK = docbook
ASCIIDOC_EXTRA  += -I../../Documentation -rasciidoctor-extensions
ASCIIDOC_EXTRA  += -alitdd='&\#x2d;&\#x2d;'
XMLTO_EXTRA     += --skip-validation
endif

ifndef SHELL_PATH
	SHELL_PATH = /bin/sh
endif
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))

MANPAGE_XSL   = ../../Documentation/manpage-normal.xsl

BUT_SUBTREE_SH := but-subtree.sh
BUT_SUBTREE    := but-subtree

BUT_SUBTREE_DOC := but-subtree.1
BUT_SUBTREE_XML := but-subtree.xml
BUT_SUBTREE_TXT := but-subtree.txt
BUT_SUBTREE_HTML := but-subtree.html
BUT_SUBTREE_TEST := ../../but-subtree

all:: $(BUT_SUBTREE)

$(BUT_SUBTREE): $(BUT_SUBTREE_SH)
	sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' $< >$@
	chmod +x $@

doc: $(BUT_SUBTREE_DOC) $(BUT_SUBTREE_HTML)

man: $(BUT_SUBTREE_DOC)

html: $(BUT_SUBTREE_HTML)

install: $(BUT_SUBTREE)
	$(INSTALL) -d -m 755 $(DESTDIR)$(butexecdir)
	$(INSTALL) -m 755 $(BUT_SUBTREE) $(DESTDIR)$(butexecdir)

install-doc: install-man install-html

install-man: $(BUT_SUBTREE_DOC)
	$(INSTALL) -d -m 755 $(DESTDIR)$(man1dir)
	$(INSTALL) -m 644 $^ $(DESTDIR)$(man1dir)

install-html: $(BUT_SUBTREE_HTML)
	$(INSTALL) -d -m 755 $(DESTDIR)$(htmldir)
	$(INSTALL) -m 644 $^ $(DESTDIR)$(htmldir)

$(BUT_SUBTREE_DOC): $(BUT_SUBTREE_XML)
	$(XMLTO) -m $(MANPAGE_XSL) $(XMLTO_EXTRA) man $^

$(BUT_SUBTREE_XML): $(BUT_SUBTREE_TXT)
	$(ASCIIDOC) -b $(ASCIIDOC_DOCBOOK) -d manpage $(ASCIIDOC_CONF) \
		-abut_version=$(BUT_VERSION) $(ASCIIDOC_EXTRA) $^

$(BUT_SUBTREE_HTML): $(BUT_SUBTREE_TXT)
	$(ASCIIDOC) -b $(ASCIIDOC_HTML) -d manpage $(ASCIIDOC_CONF) \
		-abut_version=$(BUT_VERSION) $(ASCIIDOC_EXTRA) $^

$(BUT_SUBTREE_TEST): $(BUT_SUBTREE)
	cp $< $@

test: $(BUT_SUBTREE_TEST)
	$(MAKE) -C t/ test

clean:
	$(RM) $(BUT_SUBTREE)
	$(RM) *.xml *.html *.1

.PHONY: FORCE
