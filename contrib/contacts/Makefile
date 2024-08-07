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

ASCIIDOC = asciidoc
XMLTO    = xmlto

ifndef SHELL_PATH
	SHELL_PATH = /bin/sh
endif
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))

ASCIIDOC_CONF = ../../Documentation/asciidoc.conf
MANPAGE_XSL   = ../../Documentation/manpage-normal.xsl

GIT_CONTACTS := git-contacts

GIT_CONTACTS_DOC := git-contacts.1
GIT_CONTACTS_XML := git-contacts.xml
GIT_CONTACTS_TXT := git-contacts.txt
GIT_CONTACTS_HTML := git-contacts.html

doc: $(GIT_CONTACTS_DOC) $(GIT_CONTACTS_HTML)

install: $(GIT_CONTACTS)
	$(INSTALL) -d -m 755 $(DESTDIR)$(gitexecdir)
	$(INSTALL) -m 755 $(GIT_CONTACTS) $(DESTDIR)$(gitexecdir)

install-doc: install-man install-html

install-man: $(GIT_CONTACTS_DOC)
	$(INSTALL) -d -m 755 $(DESTDIR)$(man1dir)
	$(INSTALL) -m 644 $^ $(DESTDIR)$(man1dir)

install-html: $(GIT_CONTACTS_HTML)
	$(INSTALL) -d -m 755 $(DESTDIR)$(htmldir)
	$(INSTALL) -m 644 $^ $(DESTDIR)$(htmldir)

$(GIT_CONTACTS_DOC): $(GIT_CONTACTS_XML)
	$(XMLTO) -m $(MANPAGE_XSL) man $^

$(GIT_CONTACTS_XML): $(GIT_CONTACTS_TXT)
	$(ASCIIDOC) -b docbook -d manpage -f $(ASCIIDOC_CONF) \
		-agit_version=$(GIT_VERSION) $^

$(GIT_CONTACTS_HTML): $(GIT_CONTACTS_TXT)
	$(ASCIIDOC) -b xhtml11 -d manpage -f $(ASCIIDOC_CONF) \
		-agit_version=$(GIT_VERSION) $^

clean:
	$(RM) $(GIT_CONTACTS)
	$(RM) *.xml *.html *.1

.PHONY: FORCE
