# The default target of this Makefile is...
all::

# Define V=1 to have a more verbose compile.
#
# Define JSMIN to point to JavaScript minifier that functions as
# a filter to have static/butweb.js minified.
#
# Define CSSMIN to point to a CSS minifier in order to generate a minified
# version of static/butweb.css
#

prefix ?= $(HOME)
bindir ?= $(prefix)/bin
butwebdir ?= /var/www/cgi-bin

RM ?= rm -f
INSTALL ?= install

# default configuration for butweb
BUTWEB_CONFIG = butweb_config.perl
BUTWEB_CONFIG_SYSTEM = /etc/butweb.conf
BUTWEB_CONFIG_COMMON = /etc/butweb-common.conf
BUTWEB_HOME_LINK_STR = projects
BUTWEB_SITENAME =
BUTWEB_PROJECTROOT = /pub/but
BUTWEB_PROJECT_MAXDEPTH = 2007
BUTWEB_EXPORT_OK =
BUTWEB_STRICT_EXPORT =
BUTWEB_BASE_URL =
BUTWEB_LIST =
BUTWEB_HOMETEXT = indextext.html
BUTWEB_CSS = static/butweb.css
BUTWEB_LOGO = static/but-logo.png
BUTWEB_FAVICON = static/but-favicon.png
BUTWEB_JS = static/butweb.js
BUTWEB_SITE_HTML_HEAD_STRING =
BUTWEB_SITE_HEADER =
BUTWEB_SITE_FOOTER =
HIGHLIGHT_BIN = highlight

# include user config
-include ../config.mak.autogen
-include ../config.mak
-include config.mak

# determine version
../BUT-VERSION-FILE: .FORCE-BUT-VERSION-FILE
	$(QUIET_SUBDIR0)../ $(QUIET_SUBDIR1) BUT-VERSION-FILE

ifneq ($(MAKECMDGOALS),clean)
-include ../BUT-VERSION-FILE
endif

### Build rules

SHELL_PATH ?= $(SHELL)
PERL_PATH  ?= /usr/bin/perl

# Shell quote;
bindir_SQ = $(subst ','\'',$(bindir))#'
butwebdir_SQ = $(subst ','\'',$(butwebdir))#'
butwebstaticdir_SQ = $(subst ','\'',$(butwebdir)/static)#'
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

all:: butweb.cgi static/butweb.js

BUTWEB_PROGRAMS = butweb.cgi

ifdef JSMIN
BUTWEB_FILES += static/butweb.min.js
BUTWEB_JS = static/butweb.min.js
all:: static/butweb.min.js
static/butweb.min.js: static/butweb.js BUTWEB-BUILD-OPTIONS
	$(QUIET_GEN)$(JSMIN) <$< >$@
else
BUTWEB_FILES += static/butweb.js
endif

ifdef CSSMIN
BUTWEB_FILES += static/butweb.min.css
BUTWEB_CSS = static/butweb.min.css
all:: static/butweb.min.css
static/butweb.min.css: static/butweb.css BUTWEB-BUILD-OPTIONS
	$(QUIET_GEN)$(CSSMIN) <$< >$@
else
BUTWEB_FILES += static/butweb.css
endif

BUTWEB_FILES += static/but-logo.png static/but-favicon.png

# JavaScript files that are composed (concatenated) to form butweb.js
#
# js/lib/common-lib.js should be always first, then js/lib/*.js,
# then the rest of files; js/butweb.js should be last (if it exists)
BUTWEB_JSLIB_FILES += static/js/lib/common-lib.js
BUTWEB_JSLIB_FILES += static/js/lib/datetime.js
BUTWEB_JSLIB_FILES += static/js/lib/cookies.js
BUTWEB_JSLIB_FILES += static/js/javascript-detection.js
BUTWEB_JSLIB_FILES += static/js/adjust-timezone.js
BUTWEB_JSLIB_FILES += static/js/blame_incremental.js


BUTWEB_REPLACE = \
	-e 's|++BUT_VERSION++|$(BUT_VERSION)|g' \
	-e 's|++BUT_BINDIR++|$(bindir)|g' \
	-e 's|++BUTWEB_CONFIG++|$(BUTWEB_CONFIG)|g' \
	-e 's|++BUTWEB_CONFIG_SYSTEM++|$(BUTWEB_CONFIG_SYSTEM)|g' \
	-e 's|++BUTWEB_CONFIG_COMMON++|$(BUTWEB_CONFIG_COMMON)|g' \
	-e 's|++BUTWEB_HOME_LINK_STR++|$(BUTWEB_HOME_LINK_STR)|g' \
	-e 's|++BUTWEB_SITENAME++|$(BUTWEB_SITENAME)|g' \
	-e 's|++BUTWEB_PROJECTROOT++|$(BUTWEB_PROJECTROOT)|g' \
	-e 's|"++BUTWEB_PROJECT_MAXDEPTH++"|$(BUTWEB_PROJECT_MAXDEPTH)|g' \
	-e 's|++BUTWEB_EXPORT_OK++|$(BUTWEB_EXPORT_OK)|g' \
	-e 's|++BUTWEB_STRICT_EXPORT++|$(BUTWEB_STRICT_EXPORT)|g' \
	-e 's|++BUTWEB_BASE_URL++|$(BUTWEB_BASE_URL)|g' \
	-e 's|++BUTWEB_LIST++|$(BUTWEB_LIST)|g' \
	-e 's|++BUTWEB_HOMETEXT++|$(BUTWEB_HOMETEXT)|g' \
	-e 's|++BUTWEB_CSS++|$(BUTWEB_CSS)|g' \
	-e 's|++BUTWEB_LOGO++|$(BUTWEB_LOGO)|g' \
	-e 's|++BUTWEB_FAVICON++|$(BUTWEB_FAVICON)|g' \
	-e 's|++BUTWEB_JS++|$(BUTWEB_JS)|g' \
	-e 's|++BUTWEB_SITE_HTML_HEAD_STRING++|$(BUTWEB_SITE_HTML_HEAD_STRING)|g' \
	-e 's|++BUTWEB_SITE_HEADER++|$(BUTWEB_SITE_HEADER)|g' \
	-e 's|++BUTWEB_SITE_FOOTER++|$(BUTWEB_SITE_FOOTER)|g' \
	-e 's|++HIGHLIGHT_BIN++|$(HIGHLIGHT_BIN)|g'

BUTWEB-BUILD-OPTIONS: FORCE
	@rm -f $@+
	@echo "x" '$(PERL_PATH_SQ)' $(BUTWEB_REPLACE) "$(JSMIN)|$(CSSMIN)" >$@+
	@cmp -s $@+ $@ && rm -f $@+ || mv -f $@+ $@

butweb.cgi: butweb.perl BUTWEB-BUILD-OPTIONS
	$(QUIET_GEN)$(RM) $@ $@+ && \
	sed -e '1s|#!.*perl|#!$(PERL_PATH_SQ)|' \
		$(BUTWEB_REPLACE) $< >$@+ && \
	chmod +x $@+ && \
	mv $@+ $@

static/butweb.js: $(BUTWEB_JSLIB_FILES)
	$(QUIET_GEN)$(RM) $@ $@+ && \
	cat $^ >$@+ && \
	mv $@+ $@

### Testing rules

test:
	$(MAKE) -C ../t butweb-test

test-installed:
	BUTWEB_TEST_INSTALLED='$(DESTDIR_SQ)$(butwebdir_SQ)' \
		$(MAKE) -C ../t butweb-test

### Installation rules

install: all
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(butwebdir_SQ)'
	$(INSTALL) -m 755 $(BUTWEB_PROGRAMS) '$(DESTDIR_SQ)$(butwebdir_SQ)'
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(butwebstaticdir_SQ)'
	$(INSTALL) -m 644 $(BUTWEB_FILES) '$(DESTDIR_SQ)$(butwebstaticdir_SQ)'

### Cleaning rules

clean:
	$(RM) butweb.cgi static/butweb.js \
		static/butweb.min.js static/butweb.min.css \
		BUTWEB-BUILD-OPTIONS

.PHONY: all clean install test test-installed .FORCE-BUT-VERSION-FILE FORCE

