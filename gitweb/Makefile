# The default target of this Makefile is...
all::

# Define V=1 to have a more verbose compile.
#
# Define JSMIN to point to JavaScript minifier that functions as
# a filter to have static/gitweb.js minified.
#
# Define CSSMIN to point to a CSS minifier in order to generate a minified
# version of static/gitweb.css
#

prefix ?= $(HOME)
bindir ?= $(prefix)/bin
gitwebdir ?= /var/www/cgi-bin

RM ?= rm -f
INSTALL ?= install

# default configuration for gitweb
GITWEB_CONFIG = gitweb_config.perl
GITWEB_CONFIG_SYSTEM = /etc/gitweb.conf
GITWEB_CONFIG_COMMON = /etc/gitweb-common.conf
GITWEB_HOME_LINK_STR = projects
GITWEB_SITENAME =
GITWEB_PROJECTROOT = /pub/git
GITWEB_PROJECT_MAXDEPTH = 2007
GITWEB_EXPORT_OK =
GITWEB_STRICT_EXPORT =
GITWEB_BASE_URL =
GITWEB_LIST =
GITWEB_HOMETEXT = indextext.html
GITWEB_CSS = static/gitweb.css
GITWEB_LOGO = static/git-logo.png
GITWEB_FAVICON = static/git-favicon.png
GITWEB_JS = static/gitweb.js
GITWEB_SITE_HTML_HEAD_STRING =
GITWEB_SITE_HEADER =
GITWEB_SITE_FOOTER =
HIGHLIGHT_BIN = highlight

# include user config
-include ../config.mak.autogen
-include ../config.mak
-include config.mak

# determine version
../GIT-VERSION-FILE: .FORCE-GIT-VERSION-FILE
	$(QUIET_SUBDIR0)../ $(QUIET_SUBDIR1) GIT-VERSION-FILE

-include ../GIT-VERSION-FILE

### Build rules

SHELL_PATH ?= $(SHELL)
PERL_PATH  ?= /usr/bin/perl

# Shell quote;
bindir_SQ = $(subst ','\'',$(bindir))#'
gitwebdir_SQ = $(subst ','\'',$(gitwebdir))#'
gitwebstaticdir_SQ = $(subst ','\'',$(gitwebdir)/static)#'
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))#'
PERL_PATH_SQ  = $(subst ','\'',$(PERL_PATH))#'
DESTDIR_SQ    = $(subst ','\'',$(DESTDIR))#'

# Quiet generation (unless V=1)
QUIET_SUBDIR0  = +$(MAKE) -C # space to separate -C and subdir
QUIET_SUBDIR1  =

ifneq ($(findstring $(MAKEFLAGS),w),w)
PRINT_DIR = --no-print-directory
else # "make -w"
NO_SUBDIR = :
endif

ifneq ($(findstring $(MAKEFLAGS),s),s)
ifndef V
	QUIET          = @
	QUIET_GEN      = $(QUIET)echo '   ' GEN $@;
	QUIET_SUBDIR0  = +@subdir=
	QUIET_SUBDIR1  = ;$(NO_SUBDIR) echo '   ' SUBDIR $$subdir; \
	                 $(MAKE) $(PRINT_DIR) -C $$subdir
	export V
	export QUIET
	export QUIET_GEN
	export QUIET_SUBDIR0
	export QUIET_SUBDIR1
endif
endif

all:: gitweb.cgi static/gitweb.js

GITWEB_PROGRAMS = gitweb.cgi

ifdef JSMIN
GITWEB_FILES += static/gitweb.min.js
GITWEB_JS = static/gitweb.min.js
all:: static/gitweb.min.js
static/gitweb.min.js: static/gitweb.js GITWEB-BUILD-OPTIONS
	$(QUIET_GEN)$(JSMIN) <$< >$@
else
GITWEB_FILES += static/gitweb.js
endif

ifdef CSSMIN
GITWEB_FILES += static/gitweb.min.css
GITWEB_CSS = static/gitweb.min.css
all:: static/gitweb.min.css
static/gitweb.min.css: static/gitweb.css GITWEB-BUILD-OPTIONS
	$(QUIET_GEN)$(CSSMIN) <$< >$@
else
GITWEB_FILES += static/gitweb.css
endif

GITWEB_FILES += static/git-logo.png static/git-favicon.png

# JavaScript files that are composed (concatenated) to form gitweb.js
#
# js/lib/common-lib.js should be always first, then js/lib/*.js,
# then the rest of files; js/gitweb.js should be last (if it exists)
GITWEB_JSLIB_FILES += static/js/lib/common-lib.js
GITWEB_JSLIB_FILES += static/js/lib/datetime.js
GITWEB_JSLIB_FILES += static/js/lib/cookies.js
GITWEB_JSLIB_FILES += static/js/javascript-detection.js
GITWEB_JSLIB_FILES += static/js/adjust-timezone.js
GITWEB_JSLIB_FILES += static/js/blame_incremental.js


GITWEB_REPLACE = \
	-e 's|++GIT_VERSION++|$(GIT_VERSION)|g' \
	-e 's|++GIT_BINDIR++|$(bindir)|g' \
	-e 's|++GITWEB_CONFIG++|$(GITWEB_CONFIG)|g' \
	-e 's|++GITWEB_CONFIG_SYSTEM++|$(GITWEB_CONFIG_SYSTEM)|g' \
	-e 's|++GITWEB_CONFIG_COMMON++|$(GITWEB_CONFIG_COMMON)|g' \
	-e 's|++GITWEB_HOME_LINK_STR++|$(GITWEB_HOME_LINK_STR)|g' \
	-e 's|++GITWEB_SITENAME++|$(GITWEB_SITENAME)|g' \
	-e 's|++GITWEB_PROJECTROOT++|$(GITWEB_PROJECTROOT)|g' \
	-e 's|"++GITWEB_PROJECT_MAXDEPTH++"|$(GITWEB_PROJECT_MAXDEPTH)|g' \
	-e 's|++GITWEB_EXPORT_OK++|$(GITWEB_EXPORT_OK)|g' \
	-e 's|++GITWEB_STRICT_EXPORT++|$(GITWEB_STRICT_EXPORT)|g' \
	-e 's|++GITWEB_BASE_URL++|$(GITWEB_BASE_URL)|g' \
	-e 's|++GITWEB_LIST++|$(GITWEB_LIST)|g' \
	-e 's|++GITWEB_HOMETEXT++|$(GITWEB_HOMETEXT)|g' \
	-e 's|++GITWEB_CSS++|$(GITWEB_CSS)|g' \
	-e 's|++GITWEB_LOGO++|$(GITWEB_LOGO)|g' \
	-e 's|++GITWEB_FAVICON++|$(GITWEB_FAVICON)|g' \
	-e 's|++GITWEB_JS++|$(GITWEB_JS)|g' \
	-e 's|++GITWEB_SITE_HTML_HEAD_STRING++|$(GITWEB_SITE_HTML_HEAD_STRING)|g' \
	-e 's|++GITWEB_SITE_HEADER++|$(GITWEB_SITE_HEADER)|g' \
	-e 's|++GITWEB_SITE_FOOTER++|$(GITWEB_SITE_FOOTER)|g' \
	-e 's|++HIGHLIGHT_BIN++|$(HIGHLIGHT_BIN)|g'

GITWEB-BUILD-OPTIONS: FORCE
	@rm -f $@+
	@echo "x" '$(PERL_PATH_SQ)' $(GITWEB_REPLACE) "$(JSMIN)|$(CSSMIN)" >$@+
	@cmp -s $@+ $@ && rm -f $@+ || mv -f $@+ $@

gitweb.cgi: gitweb.perl GITWEB-BUILD-OPTIONS
	$(QUIET_GEN)$(RM) $@ $@+ && \
	sed -e '1s|#!.*perl|#!$(PERL_PATH_SQ)|' \
		$(GITWEB_REPLACE) $< >$@+ && \
	chmod +x $@+ && \
	mv $@+ $@

static/gitweb.js: $(GITWEB_JSLIB_FILES)
	$(QUIET_GEN)$(RM) $@ $@+ && \
	cat $^ >$@+ && \
	mv $@+ $@

### Testing rules

test:
	$(MAKE) -C ../t gitweb-test

test-installed:
	GITWEB_TEST_INSTALLED='$(DESTDIR_SQ)$(gitwebdir_SQ)' \
		$(MAKE) -C ../t gitweb-test

### Installation rules

install: all
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(gitwebdir_SQ)'
	$(INSTALL) -m 755 $(GITWEB_PROGRAMS) '$(DESTDIR_SQ)$(gitwebdir_SQ)'
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(gitwebstaticdir_SQ)'
	$(INSTALL) -m 644 $(GITWEB_FILES) '$(DESTDIR_SQ)$(gitwebstaticdir_SQ)'

### Cleaning rules

clean:
	$(RM) gitweb.cgi static/gitweb.js \
		static/gitweb.min.js static/gitweb.min.css \
		GITWEB-BUILD-OPTIONS

.PHONY: all clean install test test-installed .FORCE-GIT-VERSION-FILE FORCE

