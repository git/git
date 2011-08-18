MAN1_TXT= \
	$(filter-out $(addsuffix .txt, $(ARTICLES) $(SP_ARTICLES)), \
		$(wildcard git-*.txt)) \
	gitk.txt git.txt
MAN5_TXT=gitattributes.txt gitignore.txt gitmodules.txt githooks.txt \
	gitrepository-layout.txt
MAN7_TXT=gitcli.txt gittutorial.txt gittutorial-2.txt \
	gitcvs-migration.txt gitcore-tutorial.txt gitglossary.txt \
	gitdiffcore.txt gitnamespaces.txt gitrevisions.txt gitworkflows.txt

MAN_TXT = $(MAN1_TXT) $(MAN5_TXT) $(MAN7_TXT)
MAN_XML=$(patsubst %.txt,%.xml,$(MAN_TXT))
MAN_HTML=$(patsubst %.txt,%.html,$(MAN_TXT))

DOC_HTML=$(MAN_HTML)

ARTICLES = howto-index
ARTICLES += everyday
ARTICLES += git-tools
ARTICLES += git-bisect-lk2009
# with their own formatting rules.
SP_ARTICLES = howto/revert-branch-rebase howto/using-merge-subtree user-manual
API_DOCS = $(patsubst %.txt,%,$(filter-out technical/api-index-skel.txt technical/api-index.txt, $(wildcard technical/api-*.txt)))
SP_ARTICLES += $(API_DOCS)
SP_ARTICLES += technical/api-index

DOC_HTML += $(patsubst %,%.html,$(ARTICLES) $(SP_ARTICLES))

DOC_MAN1=$(patsubst %.txt,%.1,$(MAN1_TXT))
DOC_MAN5=$(patsubst %.txt,%.5,$(MAN5_TXT))
DOC_MAN7=$(patsubst %.txt,%.7,$(MAN7_TXT))

prefix?=$(HOME)
bindir?=$(prefix)/bin
htmldir?=$(prefix)/share/doc/git-doc
pdfdir?=$(prefix)/share/doc/git-doc
mandir?=$(prefix)/share/man
man1dir=$(mandir)/man1
man5dir=$(mandir)/man5
man7dir=$(mandir)/man7
# DESTDIR=

ASCIIDOC=asciidoc
ASCIIDOC_EXTRA =
MANPAGE_XSL = manpage-normal.xsl
XMLTO_EXTRA =
INSTALL?=install
RM ?= rm -f
DOC_REF = origin/man
HTML_REF = origin/html

infodir?=$(prefix)/share/info
MAKEINFO=makeinfo
INSTALL_INFO=install-info
DOCBOOK2X_TEXI=docbook2x-texi
DBLATEX=dblatex
ifndef PERL_PATH
	PERL_PATH = /usr/bin/perl
endif

-include ../config.mak.autogen
-include ../config.mak

#
# For asciidoc ...
#	-7.1.2,	set ASCIIDOC7
#	8.0-,	no extra settings are needed
#

#
# For docbook-xsl ...
#	-1.68.1,	no extra settings are needed?
#	1.69.0,		set ASCIIDOC_ROFF?
#	1.69.1-1.71.0,	set DOCBOOK_SUPPRESS_SP?
#	1.71.1,		set ASCIIDOC_ROFF?
#	1.72.0,		set DOCBOOK_XSL_172.
#	1.73.0-,	no extra settings are needed
#

ifndef ASCIIDOC7
ASCIIDOC_EXTRA += -a asciidoc7compatible -a no-inline-literal
endif
ifdef DOCBOOK_XSL_172
ASCIIDOC_EXTRA += -a git-asciidoc-no-roff
MANPAGE_XSL = manpage-1.72.xsl
else
	ifndef ASCIIDOC_ROFF
	# docbook-xsl after 1.72 needs the regular XSL, but will not
	# pass-thru raw roff codes from asciidoc.conf, so turn them off.
	ASCIIDOC_EXTRA += -a git-asciidoc-no-roff
	endif
endif
ifdef MAN_BOLD_LITERAL
XMLTO_EXTRA += -m manpage-bold-literal.xsl
endif
ifdef DOCBOOK_SUPPRESS_SP
XMLTO_EXTRA += -m manpage-suppress-sp.xsl
endif

# Newer DocBook stylesheet emits warning cruft in the output when
# this is not set, and if set it shows an absolute link.  Older
# stylesheets simply ignore this parameter.
#
# Distros may want to use MAN_BASE_URL=file:///path/to/git/docs/
# or similar.
ifndef MAN_BASE_URL
MAN_BASE_URL = file://$(htmldir)/
endif
XMLTO_EXTRA += -m manpage-base-url.xsl

# If your target system uses GNU groff, it may try to render
# apostrophes as a "pretty" apostrophe using unicode.  This breaks
# cut&paste, so you should set GNU_ROFF to force them to be ASCII
# apostrophes.  Unfortunately does not work with non-GNU roff.
ifdef GNU_ROFF
XMLTO_EXTRA += -m manpage-quote-apos.xsl
endif

SHELL_PATH ?= $(SHELL)
# Shell quote;
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))

#
# Please note that there is a minor bug in asciidoc.
# The version after 6.0.3 _will_ include the patch found here:
#   http://marc.theaimsgroup.com/?l=git&m=111558757202243&w=2
#
# Until that version is released you may have to apply the patch
# yourself - yes, all 6 characters of it!
#

QUIET_SUBDIR0  = +$(MAKE) -C # space to separate -C and subdir
QUIET_SUBDIR1  =

ifneq ($(findstring $(MAKEFLAGS),w),w)
PRINT_DIR = --no-print-directory
else # "make -w"
NO_SUBDIR = :
endif

ifneq ($(findstring $(MAKEFLAGS),s),s)
ifndef V
	QUIET_ASCIIDOC	= @echo '   ' ASCIIDOC $@;
	QUIET_XMLTO	= @echo '   ' XMLTO $@;
	QUIET_DB2TEXI	= @echo '   ' DB2TEXI $@;
	QUIET_MAKEINFO	= @echo '   ' MAKEINFO $@;
	QUIET_DBLATEX	= @echo '   ' DBLATEX $@;
	QUIET_XSLTPROC	= @echo '   ' XSLTPROC $@;
	QUIET_GEN	= @echo '   ' GEN $@;
	QUIET_STDERR	= 2> /dev/null
	QUIET_SUBDIR0	= +@subdir=
	QUIET_SUBDIR1	= ;$(NO_SUBDIR) echo '   ' SUBDIR $$subdir; \
			  $(MAKE) $(PRINT_DIR) -C $$subdir
	export V
endif
endif

all: html man

html: $(DOC_HTML)

$(DOC_HTML) $(DOC_MAN1) $(DOC_MAN5) $(DOC_MAN7): asciidoc.conf

man: man1 man5 man7
man1: $(DOC_MAN1)
man5: $(DOC_MAN5)
man7: $(DOC_MAN7)

info: git.info gitman.info

pdf: user-manual.pdf

install: install-man

install-man: man
	$(INSTALL) -d -m 755 $(DESTDIR)$(man1dir)
	$(INSTALL) -d -m 755 $(DESTDIR)$(man5dir)
	$(INSTALL) -d -m 755 $(DESTDIR)$(man7dir)
	$(INSTALL) -m 644 $(DOC_MAN1) $(DESTDIR)$(man1dir)
	$(INSTALL) -m 644 $(DOC_MAN5) $(DESTDIR)$(man5dir)
	$(INSTALL) -m 644 $(DOC_MAN7) $(DESTDIR)$(man7dir)

install-info: info
	$(INSTALL) -d -m 755 $(DESTDIR)$(infodir)
	$(INSTALL) -m 644 git.info gitman.info $(DESTDIR)$(infodir)
	if test -r $(DESTDIR)$(infodir)/dir; then \
	  $(INSTALL_INFO) --info-dir=$(DESTDIR)$(infodir) git.info ;\
	  $(INSTALL_INFO) --info-dir=$(DESTDIR)$(infodir) gitman.info ;\
	else \
	  echo "No directory found in $(DESTDIR)$(infodir)" >&2 ; \
	fi

install-pdf: pdf
	$(INSTALL) -d -m 755 $(DESTDIR)$(pdfdir)
	$(INSTALL) -m 644 user-manual.pdf $(DESTDIR)$(pdfdir)

install-html: html
	'$(SHELL_PATH_SQ)' ./install-webdoc.sh $(DESTDIR)$(htmldir)

../GIT-VERSION-FILE: FORCE
	$(QUIET_SUBDIR0)../ $(QUIET_SUBDIR1) GIT-VERSION-FILE

-include ../GIT-VERSION-FILE

#
# Determine "include::" file references in asciidoc files.
#
doc.dep : $(wildcard *.txt) build-docdep.perl
	$(QUIET_GEN)$(RM) $@+ $@ && \
	$(PERL_PATH) ./build-docdep.perl >$@+ $(QUIET_STDERR) && \
	mv $@+ $@

-include doc.dep

cmds_txt = cmds-ancillaryinterrogators.txt \
	cmds-ancillarymanipulators.txt \
	cmds-mainporcelain.txt \
	cmds-plumbinginterrogators.txt \
	cmds-plumbingmanipulators.txt \
	cmds-synchingrepositories.txt \
	cmds-synchelpers.txt \
	cmds-purehelpers.txt \
	cmds-foreignscminterface.txt

$(cmds_txt): cmd-list.made

cmd-list.made: cmd-list.perl ../command-list.txt $(MAN1_TXT)
	$(QUIET_GEN)$(RM) $@ && \
	$(PERL_PATH) ./cmd-list.perl ../command-list.txt $(QUIET_STDERR) && \
	date >$@

clean:
	$(RM) *.xml *.xml+ *.html *.html+ *.1 *.5 *.7
	$(RM) *.texi *.texi+ *.texi++ git.info gitman.info
	$(RM) *.pdf
	$(RM) howto-index.txt howto/*.html doc.dep
	$(RM) technical/api-*.html technical/api-index.txt
	$(RM) $(cmds_txt) *.made
	$(RM) manpage-base-url.xsl

$(MAN_HTML): %.html : %.txt
	$(QUIET_ASCIIDOC)$(RM) $@+ $@ && \
	$(ASCIIDOC) -b xhtml11 -d manpage -f asciidoc.conf \
		$(ASCIIDOC_EXTRA) -agit_version=$(GIT_VERSION) -o $@+ $< && \
	mv $@+ $@

manpage-base-url.xsl: manpage-base-url.xsl.in
	sed "s|@@MAN_BASE_URL@@|$(MAN_BASE_URL)|" $< > $@

%.1 %.5 %.7 : %.xml manpage-base-url.xsl
	$(QUIET_XMLTO)$(RM) $@ && \
	xmlto -m $(MANPAGE_XSL) $(XMLTO_EXTRA) man $<

%.xml : %.txt
	$(QUIET_ASCIIDOC)$(RM) $@+ $@ && \
	$(ASCIIDOC) -b docbook -d manpage -f asciidoc.conf \
		$(ASCIIDOC_EXTRA) -agit_version=$(GIT_VERSION) -o $@+ $< && \
	mv $@+ $@

user-manual.xml: user-manual.txt user-manual.conf
	$(QUIET_ASCIIDOC)$(RM) $@+ $@ && \
	$(ASCIIDOC) $(ASCIIDOC_EXTRA) -b docbook -d book -o $@+ $< && \
	mv $@+ $@

technical/api-index.txt: technical/api-index-skel.txt \
	technical/api-index.sh $(patsubst %,%.txt,$(API_DOCS))
	$(QUIET_GEN)cd technical && '$(SHELL_PATH_SQ)' ./api-index.sh

$(patsubst %,%.html,$(API_DOCS) technical/api-index): %.html : %.txt
	$(QUIET_ASCIIDOC)$(ASCIIDOC) -b xhtml11 -f asciidoc.conf \
		$(ASCIIDOC_EXTRA) -agit_version=$(GIT_VERSION) $*.txt

XSLT = docbook.xsl
XSLTOPTS = --xinclude --stringparam html.stylesheet docbook-xsl.css

user-manual.html: user-manual.xml $(XSLT)
	$(QUIET_XSLTPROC)$(RM) $@+ $@ && \
	xsltproc $(XSLTOPTS) -o $@+ $(XSLT) $< && \
	mv $@+ $@

git.info: user-manual.texi
	$(QUIET_MAKEINFO)$(MAKEINFO) --no-split -o $@ user-manual.texi

user-manual.texi: user-manual.xml
	$(QUIET_DB2TEXI)$(RM) $@+ $@ && \
	$(DOCBOOK2X_TEXI) user-manual.xml --encoding=UTF-8 --to-stdout >$@++ && \
	$(PERL_PATH) fix-texi.perl <$@++ >$@+ && \
	rm $@++ && \
	mv $@+ $@

user-manual.pdf: user-manual.xml
	$(QUIET_DBLATEX)$(RM) $@+ $@ && \
	$(DBLATEX) -o $@+ -p /etc/asciidoc/dblatex/asciidoc-dblatex.xsl -s /etc/asciidoc/dblatex/asciidoc-dblatex.sty $< && \
	mv $@+ $@

gitman.texi: $(MAN_XML) cat-texi.perl
	$(QUIET_DB2TEXI)$(RM) $@+ $@ && \
	($(foreach xml,$(MAN_XML),$(DOCBOOK2X_TEXI) --encoding=UTF-8 \
		--to-stdout $(xml) &&) true) > $@++ && \
	$(PERL_PATH) cat-texi.perl $@ <$@++ >$@+ && \
	rm $@++ && \
	mv $@+ $@

gitman.info: gitman.texi
	$(QUIET_MAKEINFO)$(MAKEINFO) --no-split --no-validate $*.texi

$(patsubst %.txt,%.texi,$(MAN_TXT)): %.texi : %.xml
	$(QUIET_DB2TEXI)$(RM) $@+ $@ && \
	$(DOCBOOK2X_TEXI) --to-stdout $*.xml >$@+ && \
	mv $@+ $@

howto-index.txt: howto-index.sh $(wildcard howto/*.txt)
	$(QUIET_GEN)$(RM) $@+ $@ && \
	'$(SHELL_PATH_SQ)' ./howto-index.sh $(wildcard howto/*.txt) >$@+ && \
	mv $@+ $@

$(patsubst %,%.html,$(ARTICLES)) : %.html : %.txt
	$(QUIET_ASCIIDOC)$(ASCIIDOC) $(ASCIIDOC_EXTRA) -b xhtml11 $*.txt

WEBDOC_DEST = /pub/software/scm/git/docs

$(patsubst %.txt,%.html,$(wildcard howto/*.txt)): %.html : %.txt
	$(QUIET_ASCIIDOC)$(RM) $@+ $@ && \
	sed -e '1,/^$$/d' $< | $(ASCIIDOC) $(ASCIIDOC_EXTRA) -b xhtml11 - >$@+ && \
	mv $@+ $@

install-webdoc : html
	'$(SHELL_PATH_SQ)' ./install-webdoc.sh $(WEBDOC_DEST)

quick-install: quick-install-man

quick-install-man:
	'$(SHELL_PATH_SQ)' ./install-doc-quick.sh $(DOC_REF) $(DESTDIR)$(mandir)

quick-install-html:
	'$(SHELL_PATH_SQ)' ./install-doc-quick.sh $(HTML_REF) $(DESTDIR)$(htmldir)

.PHONY: FORCE
