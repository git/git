# Import tree-wide shared Makefile behavior and libraries
include ../shared.mak

# Guard against environment variables
MAN1_TXT =
MAN5_TXT =
MAN7_TXT =
HOWTO_TXT =
DOC_DEP_TXT =
TECH_DOCS =
ARTICLES =
SP_ARTICLES =
OBSOLETE_HTML =

-include GIT-EXCLUDED-PROGRAMS

MAN1_TXT += $(filter-out \
		$(patsubst %,%.txt,$(EXCLUDED_PROGRAMS)) \
		$(addsuffix .txt, $(ARTICLES) $(SP_ARTICLES)), \
		$(wildcard git-*.txt))
MAN1_TXT += git.txt
MAN1_TXT += gitk.txt
MAN1_TXT += gitweb.txt
MAN1_TXT += scalar.txt

# man5 / man7 guides (note: new guides should also be added to command-list.txt)
MAN5_TXT += gitattributes.txt
MAN5_TXT += gitformat-bundle.txt
MAN5_TXT += gitformat-chunk.txt
MAN5_TXT += gitformat-commit-graph.txt
MAN5_TXT += gitformat-index.txt
MAN5_TXT += gitformat-pack.txt
MAN5_TXT += gitformat-signature.txt
MAN5_TXT += githooks.txt
MAN5_TXT += gitignore.txt
MAN5_TXT += gitmailmap.txt
MAN5_TXT += gitmodules.txt
MAN5_TXT += gitprotocol-capabilities.txt
MAN5_TXT += gitprotocol-common.txt
MAN5_TXT += gitprotocol-http.txt
MAN5_TXT += gitprotocol-pack.txt
MAN5_TXT += gitprotocol-v2.txt
MAN5_TXT += gitrepository-layout.txt
MAN5_TXT += gitweb.conf.txt

MAN7_TXT += gitcli.txt
MAN7_TXT += gitcore-tutorial.txt
MAN7_TXT += gitcredentials.txt
MAN7_TXT += gitcvs-migration.txt
MAN7_TXT += gitdiffcore.txt
MAN7_TXT += giteveryday.txt
MAN7_TXT += gitfaq.txt
MAN7_TXT += gitglossary.txt
MAN7_TXT += gitnamespaces.txt
MAN7_TXT += gitremote-helpers.txt
MAN7_TXT += gitrevisions.txt
MAN7_TXT += gitsubmodules.txt
MAN7_TXT += gittutorial-2.txt
MAN7_TXT += gittutorial.txt
MAN7_TXT += gitworkflows.txt

HOWTO_TXT += $(wildcard howto/*.txt)

DOC_DEP_TXT += $(wildcard *.txt)
DOC_DEP_TXT += $(wildcard config/*.txt)
DOC_DEP_TXT += $(wildcard includes/*.txt)

ifdef MAN_FILTER
MAN_TXT = $(filter $(MAN_FILTER),$(MAN1_TXT) $(MAN5_TXT) $(MAN7_TXT))
else
MAN_TXT = $(MAN1_TXT) $(MAN5_TXT) $(MAN7_TXT)
MAN_FILTER = $(MAN_TXT)
endif

MAN_XML = $(patsubst %.txt,%.xml,$(MAN_TXT))
MAN_HTML = $(patsubst %.txt,%.html,$(MAN_TXT))
GIT_MAN_REF = master

OBSOLETE_HTML += everyday.html
OBSOLETE_HTML += git-remote-helpers.html

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
SP_ARTICLES += howto/coordinate-embargoed-releases
API_DOCS = $(patsubst %.txt,%,$(filter-out technical/api-index-skel.txt technical/api-index.txt, $(wildcard technical/api-*.txt)))
SP_ARTICLES += $(API_DOCS)

TECH_DOCS += ReviewingGuidelines
TECH_DOCS += MyFirstContribution
TECH_DOCS += MyFirstObjectWalk
TECH_DOCS += SubmittingPatches
TECH_DOCS += ToolsForGit
TECH_DOCS += technical/bitmap-format
TECH_DOCS += technical/bundle-uri
TECH_DOCS += technical/hash-function-transition
TECH_DOCS += technical/long-running-process-protocol
TECH_DOCS += technical/multi-pack-index
TECH_DOCS += technical/pack-heuristics
TECH_DOCS += technical/parallel-checkout
TECH_DOCS += technical/partial-clone
TECH_DOCS += technical/racy-git
TECH_DOCS += technical/reftable
TECH_DOCS += technical/scalar
TECH_DOCS += technical/send-pack-pipeline
TECH_DOCS += technical/shallow
TECH_DOCS += technical/trivial-merge
TECH_DOCS += technical/unit-tests
SP_ARTICLES += $(TECH_DOCS)
SP_ARTICLES += technical/api-index

ARTICLES_HTML += $(patsubst %,%.html,$(ARTICLES) $(SP_ARTICLES))
HTML_FILTER ?= $(ARTICLES_HTML) $(OBSOLETE_HTML)
DOC_HTML = $(MAN_HTML) $(filter $(HTML_FILTER),$(ARTICLES_HTML) $(OBSOLETE_HTML))

DOC_MAN1 = $(patsubst %.txt,%.1,$(filter $(MAN_FILTER),$(MAN1_TXT)))
DOC_MAN5 = $(patsubst %.txt,%.5,$(filter $(MAN_FILTER),$(MAN5_TXT)))
DOC_MAN7 = $(patsubst %.txt,%.7,$(filter $(MAN_FILTER),$(MAN7_TXT)))

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

GIT_DATE := $(shell git show --quiet --pretty='%as')

ASCIIDOC = asciidoc
ASCIIDOC_EXTRA =
ASCIIDOC_HTML = xhtml11
ASCIIDOC_DOCBOOK = docbook
ASCIIDOC_CONF = -f asciidoc.conf
ASCIIDOC_COMMON = $(ASCIIDOC) $(ASCIIDOC_EXTRA) $(ASCIIDOC_CONF) \
		-amanmanual='Git Manual' -amansource='Git $(GIT_VERSION)' \
		-arevdate='$(GIT_DATE)'
ASCIIDOC_DEPS = asciidoc.conf GIT-ASCIIDOCFLAGS
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

ifndef NO_MAN_BOLD_LITERAL
XMLTO_EXTRA += -m manpage-bold-literal.xsl
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
XMLTO_EXTRA += --stringparam man.base.url.for.relative.links='$(MAN_BASE_URL)'

ifdef USE_ASCIIDOCTOR
ASCIIDOC = asciidoctor
ASCIIDOC_CONF =
ASCIIDOC_HTML = xhtml5
ASCIIDOC_DOCBOOK = docbook5
ASCIIDOC_EXTRA += -acompat-mode -atabsize=8
ASCIIDOC_EXTRA += -I. -rasciidoctor-extensions
ASCIIDOC_EXTRA += -alitdd='&\#x2d;&\#x2d;'
ASCIIDOC_DEPS = asciidoctor-extensions.rb GIT-ASCIIDOCFLAGS
DBLATEX_COMMON =
XMLTO_EXTRA += --skip-validation
XMLTO_EXTRA += -x manpage.xsl
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

ifneq ($(filter-out lint-docs clean,$(MAKECMDGOALS)),)
-include ../GIT-VERSION-FILE
endif

#
# Determine "include::" file references in asciidoc files.
#
docdep_prereqs = \
	mergetools-list.made $(mergetools_txt) \
	cmd-list.made $(cmds_txt)

doc.dep : $(docdep_prereqs) $(DOC_DEP_TXT) build-docdep.perl
	$(QUIET_GEN)$(PERL_PATH) ./build-docdep.perl >$@ $(QUIET_STDERR)

ifneq ($(MAKECMDGOALS),clean)
-include doc.dep
endif

cmds_txt = cmds-ancillaryinterrogators.txt \
	cmds-ancillarymanipulators.txt \
	cmds-mainporcelain.txt \
	cmds-plumbinginterrogators.txt \
	cmds-plumbingmanipulators.txt \
	cmds-synchingrepositories.txt \
	cmds-synchelpers.txt \
	cmds-guide.txt \
	cmds-developerinterfaces.txt \
	cmds-userinterfaces.txt \
	cmds-purehelpers.txt \
	cmds-foreignscminterface.txt

$(cmds_txt): cmd-list.made

cmd-list.made: cmd-list.perl ../command-list.txt $(MAN1_TXT)
	$(QUIET_GEN)$(PERL_PATH) ./cmd-list.perl ../command-list.txt $(cmds_txt) $(QUIET_STDERR) && \
	date >$@

mergetools_txt = mergetools-diff.txt mergetools-merge.txt

$(mergetools_txt): mergetools-list.made

mergetools-list.made: ../git-mergetool--lib.sh $(wildcard ../mergetools/*)
	$(QUIET_GEN) \
	$(SHELL_PATH) -c 'MERGE_TOOLS_DIR=../mergetools && TOOL_MODE=diff && \
		. ../git-mergetool--lib.sh && \
		show_tool_names can_diff' | sed -e "s/\([a-z0-9]*\)/\`\1\`;;/" >mergetools-diff.txt && \
	$(SHELL_PATH) -c 'MERGE_TOOLS_DIR=../mergetools && TOOL_MODE=merge && \
		. ../git-mergetool--lib.sh && \
		show_tool_names can_merge' | sed -e "s/\([a-z0-9]*\)/\`\1\`;;/" >mergetools-merge.txt && \
	date >$@

TRACK_ASCIIDOCFLAGS = $(subst ','\'',$(ASCIIDOC_COMMON):$(ASCIIDOC_HTML):$(ASCIIDOC_DOCBOOK))

GIT-ASCIIDOCFLAGS: FORCE
	@FLAGS='$(TRACK_ASCIIDOCFLAGS)'; \
	    if test x"$$FLAGS" != x"`cat GIT-ASCIIDOCFLAGS 2>/dev/null`" ; then \
		echo >&2 "    * new asciidoc flags"; \
		echo "$$FLAGS" >GIT-ASCIIDOCFLAGS; \
            fi

clean:
	$(RM) -rf .build/
	$(RM) *.xml *.xml+ *.html *.html+ *.1 *.5 *.7
	$(RM) *.texi *.texi+ *.texi++ git.info gitman.info
	$(RM) *.pdf
	$(RM) howto-index.txt howto/*.html doc.dep
	$(RM) technical/*.html technical/api-index.txt
	$(RM) SubmittingPatches.txt
	$(RM) $(cmds_txt) $(mergetools_txt) *.made
	$(RM) GIT-ASCIIDOCFLAGS

$(MAN_HTML): %.html : %.txt $(ASCIIDOC_DEPS)
	$(QUIET_ASCIIDOC)$(TXT_TO_HTML) -d manpage -o $@ $<

$(OBSOLETE_HTML): %.html : %.txto $(ASCIIDOC_DEPS)
	$(QUIET_ASCIIDOC)$(TXT_TO_HTML) -o $@ $<

manpage-prereqs := $(wildcard manpage*.xsl)
manpage-cmd = $(QUIET_XMLTO)$(XMLTO) -m $(MANPAGE_XSL) $(XMLTO_EXTRA) man $<

%.1 : %.xml $(manpage-prereqs)
	$(manpage-cmd)
%.5 : %.xml $(manpage-prereqs)
	$(manpage-cmd)
%.7 : %.xml $(manpage-prereqs)
	$(manpage-cmd)

%.xml : %.txt $(ASCIIDOC_DEPS)
	$(QUIET_ASCIIDOC)$(TXT_TO_XML) -d manpage -o $@ $<

user-manual.xml: user-manual.txt user-manual.conf asciidoctor-extensions.rb GIT-ASCIIDOCFLAGS
	$(QUIET_ASCIIDOC)$(TXT_TO_XML) -d book -o $@ $<

technical/api-index.txt: technical/api-index-skel.txt \
	technical/api-index.sh $(patsubst %,%.txt,$(API_DOCS))
	$(QUIET_GEN)cd technical && '$(SHELL_PATH_SQ)' ./api-index.sh

technical/%.html: ASCIIDOC_EXTRA += -a git-relative-html-prefix=../
$(patsubst %,%.html,$(API_DOCS) technical/api-index $(TECH_DOCS)): %.html : %.txt \
	asciidoc.conf GIT-ASCIIDOCFLAGS
	$(QUIET_ASCIIDOC)$(TXT_TO_HTML) $*.txt

SubmittingPatches.txt: SubmittingPatches
	$(QUIET_GEN) cp $< $@

XSLT = docbook.xsl
XSLTOPTS =
XSLTOPTS += --xinclude
XSLTOPTS += --stringparam html.stylesheet docbook-xsl.css
XSLTOPTS += --param generate.consistent.ids 1

user-manual.html: user-manual.xml $(XSLT)
	$(QUIET_XSLTPROC)xsltproc $(XSLTOPTS) -o $@ $(XSLT) $<

git.info: user-manual.texi
	$(QUIET_MAKEINFO)$(MAKEINFO) --no-split -o $@ user-manual.texi

user-manual.texi: user-manual.xml
	$(QUIET_DB2TEXI)$(DOCBOOK2X_TEXI) user-manual.xml --encoding=UTF-8 --to-stdout >$@+ && \
	$(PERL_PATH) fix-texi.perl <$@+ >$@ && \
	$(RM) $@+

user-manual.pdf: user-manual.xml
	$(QUIET_DBLATEX)$(DBLATEX) -o $@ $(DBLATEX_COMMON) $<

gitman.texi: $(MAN_XML) cat-texi.perl texi.xsl
	$(QUIET_DB2TEXI) \
	($(foreach xml,$(sort $(MAN_XML)),xsltproc -o $(xml)+ texi.xsl $(xml) && \
		$(DOCBOOK2X_TEXI) --encoding=UTF-8 --to-stdout $(xml)+ && \
		$(RM) $(xml)+ &&) true) > $@+ && \
	$(PERL_PATH) cat-texi.perl $@ <$@+ >$@ && \
	$(RM) $@+

gitman.info: gitman.texi
	$(QUIET_MAKEINFO)$(MAKEINFO) --no-split --no-validate $<

$(patsubst %.txt,%.texi,$(MAN_TXT)): %.texi : %.xml
	$(QUIET_DB2TEXI)$(DOCBOOK2X_TEXI) --to-stdout $*.xml >$@

howto-index.txt: howto-index.sh $(HOWTO_TXT)
	$(QUIET_GEN)'$(SHELL_PATH_SQ)' ./howto-index.sh $(sort $(HOWTO_TXT)) >$@

$(patsubst %,%.html,$(ARTICLES)) : %.html : %.txt
	$(QUIET_ASCIIDOC)$(TXT_TO_HTML) $*.txt

WEBDOC_DEST = /pub/software/scm/git/docs

howto/%.html: ASCIIDOC_EXTRA += -a git-relative-html-prefix=../
$(patsubst %.txt,%.html,$(HOWTO_TXT)): %.html : %.txt GIT-ASCIIDOCFLAGS
	$(QUIET_ASCIIDOC) \
	sed -e '1,/^$$/d' $< | \
	$(TXT_TO_HTML) - >$@

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

## Lint: gitlink
LINT_DOCS_GITLINK = $(patsubst %.txt,.build/lint-docs/gitlink/%.ok,$(HOWTO_TXT) $(DOC_DEP_TXT))
$(LINT_DOCS_GITLINK): lint-gitlink.perl
$(LINT_DOCS_GITLINK): .build/lint-docs/gitlink/%.ok: %.txt
	$(call mkdir_p_parent_template)
	$(QUIET_LINT_GITLINK)$(PERL_PATH) lint-gitlink.perl \
		$< \
		$(HOWTO_TXT) $(DOC_DEP_TXT) \
		--section=1 $(MAN1_TXT) \
		--section=5 $(MAN5_TXT) \
		--section=7 $(MAN7_TXT) >$@
.PHONY: lint-docs-gitlink
lint-docs-gitlink: $(LINT_DOCS_GITLINK)

## Lint: man-end-blurb
LINT_DOCS_MAN_END_BLURB = $(patsubst %.txt,.build/lint-docs/man-end-blurb/%.ok,$(MAN_TXT))
$(LINT_DOCS_MAN_END_BLURB): lint-man-end-blurb.perl
$(LINT_DOCS_MAN_END_BLURB): .build/lint-docs/man-end-blurb/%.ok: %.txt
	$(call mkdir_p_parent_template)
	$(QUIET_LINT_MANEND)$(PERL_PATH) lint-man-end-blurb.perl $< >$@
.PHONY: lint-docs-man-end-blurb

## Lint: man-section-order
LINT_DOCS_MAN_SECTION_ORDER = $(patsubst %.txt,.build/lint-docs/man-section-order/%.ok,$(MAN_TXT))
$(LINT_DOCS_MAN_SECTION_ORDER): lint-man-section-order.perl
$(LINT_DOCS_MAN_SECTION_ORDER): .build/lint-docs/man-section-order/%.ok: %.txt
	$(call mkdir_p_parent_template)
	$(QUIET_LINT_MANSEC)$(PERL_PATH) lint-man-section-order.perl $< >$@
.PHONY: lint-docs-man-section-order
lint-docs-man-section-order: $(LINT_DOCS_MAN_SECTION_ORDER)

.PHONY: lint-docs-fsck-msgids
LINT_DOCS_FSCK_MSGIDS = .build/lint-docs/fsck-msgids.ok
$(LINT_DOCS_FSCK_MSGIDS): lint-fsck-msgids.perl
$(LINT_DOCS_FSCK_MSGIDS): ../fsck.h fsck-msgids.txt
	$(call mkdir_p_parent_template)
	$(QUIET_GEN)$(PERL_PATH) lint-fsck-msgids.perl \
		../fsck.h fsck-msgids.txt $@

lint-docs-fsck-msgids: $(LINT_DOCS_FSCK_MSGIDS)

## Lint: list of targets above
.PHONY: lint-docs
lint-docs: lint-docs-fsck-msgids
lint-docs: lint-docs-gitlink
lint-docs: lint-docs-man-end-blurb
lint-docs: lint-docs-man-section-order

ifeq ($(wildcard po/Makefile),po/Makefile)
doc-l10n install-l10n::
	$(MAKE) -C po $@
endif

.PHONY: FORCE
