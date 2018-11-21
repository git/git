# Guard against environment variables
MAN1_TXT =
MAN5_TXT =
MAN7_TXT =
TECH_DOCS =
ARTICLES =
SP_ARTICLES =
OBSOLETE_HTML =

MAN1_TXT += $(filter-out \
		$(addsuffix .txt, $(ARTICLES) $(SP_ARTICLES)), \
		$(wildcard git-*.txt))
MAN1_TXT += git.txt
MAN1_TXT += gitk.txt
MAN1_TXT += gitremote-helpers.txt
MAN1_TXT += gitweb.txt

MAN5_TXT += gitattributes.txt
MAN5_TXT += githooks.txt
MAN5_TXT += gitignore.txt
MAN5_TXT += gitmodules.txt
MAN5_TXT += gitrepository-layout.txt
MAN5_TXT += gitweb.conf.txt

MAN7_TXT += gitcli.txt
MAN7_TXT += gitcore-tutorial.txt
MAN7_TXT += gitcredentials.txt
MAN7_TXT += gitcvs-migration.txt
MAN7_TXT += gitdiffcore.txt
MAN7_TXT += giteveryday.txt
MAN7_TXT += gitglossary.txt
MAN7_TXT += gitnamespaces.txt
MAN7_TXT += gitrevisions.txt
MAN7_TXT += gitsubmodules.txt
MAN7_TXT += gittutorial-2.txt
MAN7_TXT += gittutorial.txt
MAN7_TXT += gitworkflows.txt

MAN_TXT = $(MAN1_TXT) $(MAN5_TXT) $(MAN7_TXT)
MAN_XML = $(patsubst %.txt,%.xml,$(MAN_TXT))
MAN_HTML = $(patsubst %.txt,%.html,$(MAN_TXT))
GIT_MAN_REF = master

OBSOLETE_HTML += everyday.html
OBSOLETE_HTML += git-remote-helpers.html
DOC_HTML = $(MAN_HTML) $(OBSOLETE_HTML)

ARTICLES += howto-index
ARTICLES += git-tools
ARTICLES += git-bisect-lk2009
# with their own formatting rules.
SP_ARTICLES += user-manual
SP_ARTICLES += howto/new-command
SP_ARTICLES += howto/revert-branch-rebase
SP_ARTICLES += howto/using-merge-subtree
SP_ARTICLES += howto/using-signed-tag-in-pull-request
SP_ARTICLES += howto/use-git-daemon
SP_ARTICLES += howto/update-hook-example
SP_ARTICLES += howto/setup-git-server-over-http
SP_ARTICLES += howto/separating-topic-branches
SP_ARTICLES += howto/revert-a-faulty-merge
SP_ARTICLES += howto/recover-corrupted-blob-object
SP_ARTICLES += howto/recover-corrupted-object-harder
SP_ARTICLES += howto/rebuild-from-update-hook
SP_ARTICLES += howto/rebase-from-internal-branch
SP_ARTICLES += howto/keep-canonical-history-correct
SP_ARTICLES += howto/maintain-git
API_DOCS = $(patsubst %.txt,%,$(filter-out technical/api-index-skel.txt technical/api-index.txt, $(wildcard technical/api-*.txt)))
SP_ARTICLES += $(API_DOCS)

TECH_DOCS += SubmittingPatches
TECH_DOCS += technical/hash-function-transition
TECH_DOCS += technical/http-protocol
TECH_DOCS += technical/index-format
TECH_DOCS += technical/long-running-process-protocol
TECH_DOCS += technical/multi-pack-index
TECH_DOCS += technical/pack-format
TECH_DOCS += technical/pack-heuristics
TECH_DOCS += technical/pack-protocol
TECH_DOCS += technical/partial-clone
TECH_DOCS += technical/protocol-capabilities
TECH_DOCS += technical/protocol-common
TECH_DOCS += technical/protocol-v2
TECH_DOCS += technical/racy-git
TECH_DOCS += technical/send-pack-pipeline
TECH_DOCS += technical/shallow
TECH_DOCS += technical/signature-format
TECH_DOCS += technical/trivial-merge
SP_ARTICLES += $(TECH_DOCS)
SP_ARTICLES += technical/api-index

DOC_HTML += $(patsubst %,%.html,$(ARTICLES) $(SP_ARTICLES))

DOC_MAN1 = $(patsubst %.txt,%.1,$(MAN1_TXT))
DOC_MAN5 = $(patsubst %.txt,%.5,$(MAN5_TXT))
DOC_MAN7 = $(patsubst %.txt,%.7,$(MAN7_TXT))

prefix ?= $(HOME)
bindir ?= $(prefix)/bin
htmldir ?= $(prefix)/share/doc/git-doc
infodir ?= $(prefix)/share/info
pdfdir ?= $(prefix)/share/doc/git-doc
mandir ?= $(prefix)/share/man
man1dir = $(mandir)/man1
man5dir = $(mandir)/man5
man7dir = $(mandir)/man7
# DESTDIR =

ASCIIDOC = asciidoc
ASCIIDOC_EXTRA =
ASCIIDOC_HTML = xhtml11
ASCIIDOC_DOCBOOK = docbook
ASCIIDOC_CONF = -f asciidoc.conf
ASCIIDOC_COMMON = $(ASCIIDOC) $(ASCIIDOC_EXTRA) $(ASCIIDOC_CONF) \
		-agit_version=$(GIT_VERSION)
TXT_TO_HTML = $(ASCIIDOC_COMMON) -b $(ASCIIDOC_HTML)
TXT_TO_XML = $(ASCIIDOC_COMMON) -b $(ASCIIDOC_DOCBOOK)
MANPAGE_XSL = manpage-normal.xsl
XMLTO = xmlto
XMLTO_EXTRA =
INSTALL ?= install
RM ?= rm -f
MAN_REPO = ../../git-manpages
HTML_REPO = ../../git-htmldocs

MAKEINFO = makeinfo
INSTALL_INFO = install-info
DOCBOOK2X_TEXI = docbook2x-texi
DBLATEX = dblatex
ASCIIDOC_DBLATEX_DIR = /etc/asciidoc/dblatex
DBLATEX_COMMON = -p $(ASCIIDOC_DBLATEX_DIR)/asciidoc-dblatex.xsl -s $(ASCIIDOC_DBLATEX_DIR)/asciidoc-dblatex.sty
ifndef PERL_PATH
	PERL_PATH = /usr/bin/perl
endif

-include ../config.mak.autogen
-include ../config.mak

#
# For docbook-xsl ...
#	-1.68.1,	no extra settings are needed?
#	1.69.0,		set ASCIIDOC_ROFF?
#	1.69.1-1.71.0,	set DOCBOOK_SUPPRESS_SP?
#	1.71.1,		set ASCIIDOC_ROFF?
#	1.72.0,		set DOCBOOK_XSL_172.
#	1.73.0-,	no extra settings are needed
#

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
ifndef NO_MAN_BOLD_LITERAL
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

ifdef USE_ASCIIDOCTOR
ASCIIDOC = asciidoctor
ASCIIDOC_CONF =
ASCIIDOC_HTML = xhtml5
ASCIIDOC_DOCBOOK = docbook45
ASCIIDOC_EXTRA += -acompat-mode -atabsize=8
ASCIIDOC_EXTRA += -I. -rasciidoctor-extensions
ASCIIDOC_EXTRA += -alitdd='&\#x2d;&\#x2d;'
DBLATEX_COMMON =
endif

SHELL_PATH ?= $(SHELL)
# Shell quote;
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))

ifdef DEFAULT_PAGER
DEFAULT_PAGER_SQ = $(subst ','\'',$(DEFAULT_PAGER))
ASCIIDOC_EXTRA += -a 'git-default-pager=$(DEFAULT_PAGER_SQ)'
endif

ifdef DEFAULT_EDITOR
DEFAULT_EDITOR_SQ = $(subst ','\'',$(DEFAULT_EDITOR))
ASCIIDOC_EXTRA += -a 'git-default-editor=$(DEFAULT_EDITOR_SQ)'
endif

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
	QUIET_LINT	= @echo '   ' LINT $@;
	QUIET_STDERR	= 2> /dev/null
	QUIET_SUBDIR0	= +@subdir=
	QUIET_SUBDIR1	= ;$(NO_SUBDIR) echo '   ' SUBDIR $$subdir; \
			  $(MAKE) $(PRINT_DIR) -C $$subdir
	export V
endif
endif

all: html man

html: $(DOC_HTML)

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
docdep_prereqs = \
	mergetools-list.made $(mergetools_txt) \
	cmd-list.made $(cmds_txt)

doc.dep : $(docdep_prereqs) $(wildcard *.txt) $(wildcard config/*.txt) build-docdep.perl
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

mergetools_txt = mergetools-diff.txt mergetools-merge.txt

$(mergetools_txt): mergetools-list.made

mergetools-list.made: ../git-mergetool--lib.sh $(wildcard ../mergetools/*)
	$(QUIET_GEN)$(RM) $@ && \
	$(SHELL_PATH) -c 'MERGE_TOOLS_DIR=../mergetools && \
		. ../git-mergetool--lib.sh && \
		show_tool_names can_diff "* " || :' >mergetools-diff.txt && \
	$(SHELL_PATH) -c 'MERGE_TOOLS_DIR=../mergetools && \
		. ../git-mergetool--lib.sh && \
		show_tool_names can_merge "* " || :' >mergetools-merge.txt && \
	date >$@

clean:
	$(RM) *.xml *.xml+ *.html *.html+ *.1 *.5 *.7
	$(RM) *.texi *.texi+ *.texi++ git.info gitman.info
	$(RM) *.pdf
	$(RM) howto-index.txt howto/*.html doc.dep
	$(RM) technical/*.html technical/api-index.txt
	$(RM) SubmittingPatches.txt
	$(RM) $(cmds_txt) $(mergetools_txt) *.made
	$(RM) manpage-base-url.xsl

$(MAN_HTML): %.html : %.txt asciidoc.conf
	$(QUIET_ASCIIDOC)$(RM) $@+ $@ && \
	$(TXT_TO_HTML) -d manpage -o $@+ $< && \
	mv $@+ $@

$(OBSOLETE_HTML): %.html : %.txto asciidoc.conf
	$(QUIET_ASCIIDOC)$(RM) $@+ $@ && \
	$(TXT_TO_HTML) -o $@+ $< && \
	mv $@+ $@

manpage-base-url.xsl: manpage-base-url.xsl.in
	$(QUIET_GEN)sed "s|@@MAN_BASE_URL@@|$(MAN_BASE_URL)|" $< > $@

%.1 %.5 %.7 : %.xml manpage-base-url.xsl
	$(QUIET_XMLTO)$(RM) $@ && \
	$(XMLTO) -m $(MANPAGE_XSL) $(XMLTO_EXTRA) man $<

%.xml : %.txt asciidoc.conf
	$(QUIET_ASCIIDOC)$(RM) $@+ $@ && \
	$(TXT_TO_XML) -d manpage -o $@+ $< && \
	mv $@+ $@

user-manual.xml: user-manual.txt user-manual.conf
	$(QUIET_ASCIIDOC)$(RM) $@+ $@ && \
	$(TXT_TO_XML) -d book -o $@+ $< && \
	mv $@+ $@

technical/api-index.txt: technical/api-index-skel.txt \
	technical/api-index.sh $(patsubst %,%.txt,$(API_DOCS))
	$(QUIET_GEN)cd technical && '$(SHELL_PATH_SQ)' ./api-index.sh

technical/%.html: ASCIIDOC_EXTRA += -a git-relative-html-prefix=../
$(patsubst %,%.html,$(API_DOCS) technical/api-index $(TECH_DOCS)): %.html : %.txt asciidoc.conf
	$(QUIET_ASCIIDOC)$(TXT_TO_HTML) $*.txt

SubmittingPatches.txt: SubmittingPatches
	$(QUIET_GEN) cp $< $@

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
	$(DBLATEX) -o $@+ $(DBLATEX_COMMON) $< && \
	mv $@+ $@

gitman.texi: $(MAN_XML) cat-texi.perl texi.xsl
	$(QUIET_DB2TEXI)$(RM) $@+ $@ && \
	($(foreach xml,$(sort $(MAN_XML)),xsltproc -o $(xml)+ texi.xsl $(xml) && \
		$(DOCBOOK2X_TEXI) --encoding=UTF-8 --to-stdout $(xml)+ && \
		rm $(xml)+ &&) true) > $@++ && \
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
	'$(SHELL_PATH_SQ)' ./howto-index.sh $(sort $(wildcard howto/*.txt)) >$@+ && \
	mv $@+ $@

$(patsubst %,%.html,$(ARTICLES)) : %.html : %.txt
	$(QUIET_ASCIIDOC)$(TXT_TO_HTML) $*.txt

WEBDOC_DEST = /pub/software/scm/git/docs

howto/%.html: ASCIIDOC_EXTRA += -a git-relative-html-prefix=../
$(patsubst %.txt,%.html,$(wildcard howto/*.txt)): %.html : %.txt
	$(QUIET_ASCIIDOC)$(RM) $@+ $@ && \
	sed -e '1,/^$$/d' $< | \
	$(TXT_TO_HTML) - >$@+ && \
	mv $@+ $@

install-webdoc : html
	'$(SHELL_PATH_SQ)' ./install-webdoc.sh $(WEBDOC_DEST)

# You must have a clone of 'git-htmldocs' and 'git-manpages' repositories
# next to the 'git' repository itself for the following to work.

quick-install: quick-install-man

require-manrepo::
	@if test ! -d $(MAN_REPO); \
	then echo "git-manpages repository must exist at $(MAN_REPO)"; exit 1; fi

quick-install-man: require-manrepo
	'$(SHELL_PATH_SQ)' ./install-doc-quick.sh $(MAN_REPO) $(DESTDIR)$(mandir) $(GIT_MAN_REF)

require-htmlrepo::
	@if test ! -d $(HTML_REPO); \
	then echo "git-htmldocs repository must exist at $(HTML_REPO)"; exit 1; fi

quick-install-html: require-htmlrepo
	'$(SHELL_PATH_SQ)' ./install-doc-quick.sh $(HTML_REPO) $(DESTDIR)$(htmldir) $(GIT_MAN_REF)

print-man1:
	@for i in $(MAN1_TXT); do echo $$i; done

lint-docs::
	$(QUIET_LINT)$(PERL_PATH) lint-gitlink.perl

.PHONY: FORCE
