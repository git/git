# The default target of this Makefile is...
all::

-include ../../shared.mak
-include ../../config.mak.autogen
-include ../../config.mak

prefix ?= /usr/local
gitexecdir ?= $(prefix)/libexec/git-core
mandir ?= $(prefix)/share/man
man1dir ?= $(mandir)/man1
htmldir ?= $(prefix)/share/doc/git-doc

../../GIT-VERSION-FILE: FORCE
	$(MAKE) -C ../../ GIT-VERSION-FILE

# this should be set to a 'standard' bsd-type install program
INSTALL  ?= install
RM       ?= rm -f

ASCIIDOC         = asciidoc
ASCIIDOC_CONF    = -f asciidoc.conf
ASCIIDOC_HTML    = xhtml11
ASCIIDOC_DOCBOOK = docbook
ASCIIDOC_EXTRA   =
ASCIIDOC_DEPS    = asciidoc.conf
XMLTO            = xmlto
XMLTO_EXTRA      =

ifdef USE_ASCIIDOCTOR
ASCIIDOC         = asciidoctor
ASCIIDOC_CONF    =
ASCIIDOC_HTML    = xhtml5
ASCIIDOC_DOCBOOK = docbook
ASCIIDOC_EXTRA  += -I. -rasciidoctor-extensions
ASCIIDOC_EXTRA  += -alitdd='&\#x2d;&\#x2d;'
ASCIIDOC_DEPS    = asciidoctor-extensions.rb
XMLTO_EXTRA     += --skip-validation
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
GIT_SUBTREE_TXT := git-subtree.adoc
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
	$(XMLTO) -m $(MANPAGE_XSL) $(XMLTO_EXTRA) man $^

$(GIT_SUBTREE_XML): $(GIT_SUBTREE_TXT) $(ASCIIDOC_DEPS)
	$(ASCIIDOC) -b $(ASCIIDOC_DOCBOOK) -d manpage $(ASCIIDOC_CONF) \
		$(ASCIIDOC_EXTRA) $<

$(GIT_SUBTREE_HTML): $(GIT_SUBTREE_TXT) $(ASCIIDOC_DEPS)
	$(ASCIIDOC) -b $(ASCIIDOC_HTML) -d manpage $(ASCIIDOC_CONF) \
		$(ASCIIDOC_EXTRA) $<

$(GIT_SUBTREE_TEST): $(GIT_SUBTREE)
	cp $< $@

test: $(GIT_SUBTREE_TEST)
	$(MAKE) -C t/ test

clean:
	$(RM) $(GIT_SUBTREE)
	$(RM) asciidoc.conf asciidoctor-extensions.rb
	$(RM) *.xml *.html *.1

asciidoc.conf: ../../Documentation/asciidoc.conf.in ../../GIT-VERSION-FILE
	$(QUIET_GEN)$(call version_gen,"$(shell pwd)/../..",$<,$@)
asciidoctor-extensions.rb: ../../Documentation/asciidoctor-extensions.rb.in ../../GIT-VERSION-FILE
	$(QUIET_GEN)$(call version_gen,"$(shell pwd)/../..",$<,$@)

.PHONY: FORCE
