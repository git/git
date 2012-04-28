-include ../../config.mak.autogen
-include ../../config.mak

prefix ?= /usr/local
mandir ?= $(prefix)/share/man
libexecdir ?= $(prefix)/libexec/git-core
gitdir ?= $(shell git --exec-path)
man1dir ?= $(mandir)/man1

gitver ?= $(word 3,$(shell git --version))

# this should be set to a 'standard' bsd-type install program
INSTALL ?= install

ASCIIDOC_CONF      = ../../Documentation/asciidoc.conf
MANPAGE_NORMAL_XSL =  ../../Documentation/manpage-normal.xsl

GIT_SUBTREE_SH := git-subtree.sh
GIT_SUBTREE    := git-subtree

GIT_SUBTREE_DOC := git-subtree.1
GIT_SUBTREE_XML := git-subtree.xml
GIT_SUBTREE_TXT := git-subtree.txt

all: $(GIT_SUBTREE)

$(GIT_SUBTREE): $(GIT_SUBTREE_SH)
	cp $< $@ && chmod +x $@

doc: $(GIT_SUBTREE_DOC)

install: $(GIT_SUBTREE)
	$(INSTALL) -m 755 $(GIT_SUBTREE) $(libexecdir)

install-doc: install-man

install-man: $(GIT_SUBTREE_DOC)
	$(INSTALL) -m 644 $^ $(man1dir)

$(GIT_SUBTREE_DOC): $(GIT_SUBTREE_XML)
	xmlto -m $(MANPAGE_NORMAL_XSL)  man $^

$(GIT_SUBTREE_XML): $(GIT_SUBTREE_TXT)
	asciidoc -b docbook -d manpage -f $(ASCIIDOC_CONF) \
		-agit_version=$(gitver) $^

test:
	$(MAKE) -C t/ test

clean:
	rm -f *~ *.xml *.html *.1
	rm -rf subproj mainline
