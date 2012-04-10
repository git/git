-include ../../config.mak.autogen
-include ../../config.mak

prefix ?= /usr/local
mandir ?= $(prefix)/share/man
gitdir ?= $(shell git --exec-path)

gitver ?= $(word 3,$(shell git --version))

# this should be set to a 'standard' bsd-type install program
INSTALL ?= install
INSTALL_DATA = $(INSTALL) -c -m 0644
INSTALL_EXE = $(INSTALL) -c -m 0755
INSTALL_DIR = $(INSTALL) -c -d -m 0755

ASCIIDOC_CONF      = ../../Documentation/asciidoc.conf
MANPAGE_NORMAL_XSL =  ../../Documentation/manpage-normal.xsl

default:
	@echo "git-subtree doesn't need to be built."
	@echo "Just copy it somewhere on your PATH, like /usr/local/bin."
	@echo
	@echo "Try: make doc"
	@echo " or: make test"
	@false

install: install-exe install-doc

install-exe: git-subtree.sh
	$(INSTALL_DIR) $(DESTDIR)/$(gitdir)
	$(INSTALL_EXE) $< $(DESTDIR)/$(gitdir)/git-subtree

install-doc: git-subtree.1
	$(INSTALL_DIR) $(DESTDIR)/$(mandir)/man1/
	$(INSTALL_DATA) $< $(DESTDIR)/$(mandir)/man1/

doc: git-subtree.1

%.1: %.xml
	xmlto -m $(MANPAGE_NORMAL_XSL)  man $^

%.xml: %.txt
	asciidoc -b docbook -d manpage -f $(ASCIIDOC_CONF) \
		-agit_version=$(gitver) $^

test:
	./test.sh

clean:
	rm -f *~ *.xml *.html *.1
	rm -rf subproj mainline
