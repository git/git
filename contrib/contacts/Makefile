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

ASCIIDOC = asciidoc
XMLTO    = xmlto

ifndef SHELL_PATH
	SHELL_PATH = /bin/sh
endif
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))

ASCIIDOC_CONF = ../../Documentation/asciidoc.conf
MANPAGE_XSL   = ../../Documentation/manpage-normal.xsl

BUT_CONTACTS := but-contacts

BUT_CONTACTS_DOC := but-contacts.1
BUT_CONTACTS_XML := but-contacts.xml
BUT_CONTACTS_TXT := but-contacts.txt
BUT_CONTACTS_HTML := but-contacts.html

doc: $(BUT_CONTACTS_DOC) $(BUT_CONTACTS_HTML)

install: $(BUT_CONTACTS)
	$(INSTALL) -d -m 755 $(DESTDIR)$(butexecdir)
	$(INSTALL) -m 755 $(BUT_CONTACTS) $(DESTDIR)$(butexecdir)

install-doc: install-man install-html

install-man: $(BUT_CONTACTS_DOC)
	$(INSTALL) -d -m 755 $(DESTDIR)$(man1dir)
	$(INSTALL) -m 644 $^ $(DESTDIR)$(man1dir)

install-html: $(BUT_CONTACTS_HTML)
	$(INSTALL) -d -m 755 $(DESTDIR)$(htmldir)
	$(INSTALL) -m 644 $^ $(DESTDIR)$(htmldir)

$(BUT_CONTACTS_DOC): $(BUT_CONTACTS_XML)
	$(XMLTO) -m $(MANPAGE_XSL) man $^

$(BUT_CONTACTS_XML): $(BUT_CONTACTS_TXT)
	$(ASCIIDOC) -b docbook -d manpage -f $(ASCIIDOC_CONF) \
		-abut_version=$(BUT_VERSION) $^

$(BUT_CONTACTS_HTML): $(BUT_CONTACTS_TXT)
	$(ASCIIDOC) -b xhtml11 -d manpage -f $(ASCIIDOC_CONF) \
		-abut_version=$(BUT_VERSION) $^

clean:
	$(RM) $(BUT_CONTACTS)
	$(RM) *.xml *.html *.1

.PHONY: FORCE
