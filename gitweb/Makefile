ifndef MAK_DIR_GITWEB
$(error do not run gitweb/Makefile stand-alone anymore. The "gitweb" and \
"install-gitweb" targets now live in the top-level Makefile)
endif

# Define JSMIN to point to JavaScript minifier that functions as
# a filter to have static/gitweb.js minified.
#
# Define CSSMIN to point to a CSS minifier in order to generate a minified
# version of static/gitweb.css
#

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
GITWEB_CSS_IN = static/gitweb.css
GITWEB_CSS = $(GITWEB_CSS_IN)
GITWEB_LOGO = static/git-logo.png
GITWEB_FAVICON = static/git-favicon.png
GITWEB_JS_IN = static/gitweb.js
GITWEB_JS = $(GITWEB_JS_IN)
GITWEB_SITE_HTML_HEAD_STRING =
GITWEB_SITE_HEADER =
GITWEB_SITE_FOOTER =
HIGHLIGHT_BIN = highlight

# What targets we'll add to 'all' for "make gitweb"
GITWEB_ALL =
GITWEB_ALL += gitweb.cgi
GITWEB_ALL += $(GITWEB_JS)

MAK_DIR_GITWEB_ALL = $(addprefix $(MAK_DIR_GITWEB),$(GITWEB_ALL))

GITWEB_PROGRAMS = gitweb.cgi

GITWEB_JS_MIN = static/gitweb.min.js
ifdef JSMIN
GITWEB_JS = $(GITWEB_JS_MIN)
GITWEB_ALL += $(MAK_DIR_GITWEB)$(GITWEB_JS_MIN)
$(MAK_DIR_GITWEB)$(GITWEB_JS_MIN): $(MAK_DIR_GITWEB)GITWEB-BUILD-OPTIONS
$(MAK_DIR_GITWEB)$(GITWEB_JS_MIN): $(MAK_DIR_GITWEB)$(GITWEB_JS_IN)
	$(QUIET_GEN)$(JSMIN) <$< >$@
endif
GITWEB_FILES += $(GITWEB_JS)

GITWEB_CSS_MIN = static/gitweb.min.css
ifdef CSSMIN
GITWEB_CSS = $(GITWEB_CSS_MIN)
GITWEB_ALL += $(MAK_DIR_GITWEB)$(GITWEB_CSS_MIN)
$(MAK_DIR_GITWEB)$(GITWEB_CSS_MIN): $(MAK_DIR_GITWEB)GITWEB-BUILD-OPTIONS
$(MAK_DIR_GITWEB)$(GITWEB_CSS_MIN): $(MAK_DIR_GITWEB)$(GITWEB_CSS_IN)
	$(QUIET_GEN)$(CSSMIN) <$< >$@
endif
GITWEB_FILES += $(GITWEB_CSS)

GITWEB_FILES += static/git-logo.png static/git-favicon.png

# JavaScript files that are composed (concatenated) to form gitweb.js
#
# js/lib/common-lib.js should be always first, then js/lib/*.js,
# then the rest of files; js/gitweb.js should be last (if it exists)
GITWEB_JSLIB_FILES =
GITWEB_JSLIB_FILES += static/js/lib/common-lib.js
GITWEB_JSLIB_FILES += static/js/lib/datetime.js
GITWEB_JSLIB_FILES += static/js/lib/cookies.js
GITWEB_JSLIB_FILES += static/js/javascript-detection.js
GITWEB_JSLIB_FILES += static/js/adjust-timezone.js
GITWEB_JSLIB_FILES += static/js/blame_incremental.js

.PHONY: FORCE
$(MAK_DIR_GITWEB)GITWEB-BUILD-OPTIONS: FORCE
	@sed -e 's|@PERL_PATH@|$(PERL_PATH_SQ)|' \
	     -e 's|@JSMIN@|$(JSMIN)|' \
	     -e 's|@CSSMIN@|$(CSSMIN)|' \
	     -e 's|@GIT_VERSION@|$(GIT_VERSION)|' \
	     -e 's|@GIT_BINDIR@|$(bindir)|' \
	     -e 's|@GITWEB_CONFIG@|$(GITWEB_CONFIG)|' \
	     -e 's|@GITWEB_CONFIG_SYSTEM@|$(GITWEB_CONFIG_SYSTEM)|' \
	     -e 's|@GITWEB_CONFIG_COMMON@|$(GITWEB_CONFIG_COMMON)|' \
	     -e 's|@GITWEB_HOME_LINK_STR@|$(GITWEB_HOME_LINK_STR)|' \
	     -e 's|@GITWEB_SITENAME@|$(GITWEB_SITENAME)|' \
	     -e 's|@GITWEB_PROJECTROOT@|$(GITWEB_PROJECTROOT)|' \
	     -e 's|@GITWEB_PROJECT_MAXDEPTH@|$(GITWEB_PROJECT_MAXDEPTH)|' \
	     -e 's|@GITWEB_EXPORT_OK@|$(GITWEB_EXPORT_OK)|' \
	     -e 's|@GITWEB_STRICT_EXPORT@|$(GITWEB_STRICT_EXPORT)|' \
	     -e 's|@GITWEB_BASE_URL@|$(GITWEB_BASE_URL)|' \
	     -e 's|@GITWEB_LIST@|$(GITWEB_LIST)|' \
	     -e 's|@GITWEB_HOMETEXT@|$(GITWEB_HOMETEXT)|' \
	     -e 's|@GITWEB_CSS@|$(GITWEB_CSS)|' \
	     -e 's|@GITWEB_LOGO@|$(GITWEB_LOGO)|' \
	     -e 's|@GITWEB_FAVICON@|$(GITWEB_FAVICON)|' \
	     -e 's|@GITWEB_JS@|$(GITWEB_JS)|' \
	     -e 's|@GITWEB_SITE_HTML_HEAD_STRING@|$(GITWEB_SITE_HTML_HEAD_STRING)|' \
	     -e 's|@GITWEB_SITE_HEADER@|$(GITWEB_SITE_HEADER)|' \
	     -e 's|@GITWEB_SITE_FOOTER@|$(GITWEB_SITE_FOOTER)|' \
	     -e 's|@HIGHLIGHT_BIN@|$(HIGHLIGHT_BIN)|' \
	     $(MAK_DIR_GITWEB)GITWEB-BUILD-OPTIONS.in >"$@+"
	@cmp -s $@+ $@ && rm -f $@+ || mv -f $@+ $@

$(MAK_DIR_GITWEB)gitweb.cgi: $(MAK_DIR_GITWEB)generate-gitweb-cgi.sh
$(MAK_DIR_GITWEB)gitweb.cgi: $(MAK_DIR_GITWEB)GITWEB-BUILD-OPTIONS
$(MAK_DIR_GITWEB)gitweb.cgi: GIT-VERSION-FILE
$(MAK_DIR_GITWEB)gitweb.cgi: $(MAK_DIR_GITWEB)gitweb.perl
	$(QUIET_GEN)$(RM) $@ $@+ && \
	$(MAK_DIR_GITWEB)generate-gitweb-cgi.sh $(MAK_DIR_GITWEB)/GITWEB-BUILD-OPTIONS ./GIT-VERSION-FILE $< $@+ && \
	mv $@+ $@

$(MAK_DIR_GITWEB)static/gitweb.js: $(MAK_DIR_GITWEB)generate-gitweb-js.sh
$(MAK_DIR_GITWEB)static/gitweb.js: $(addprefix $(MAK_DIR_GITWEB),$(GITWEB_JSLIB_FILES))
	$(QUIET_GEN)$(RM) $@ $@+ && \
	$(MAK_DIR_GITWEB)generate-gitweb-js.sh $@+ $^ && \
	mv $@+ $@

### Installation rules

.PHONY: install-gitweb
install-gitweb: $(MAK_DIR_GITWEB_ALL)
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(gitwebdir_SQ)'
	$(INSTALL) -m 755 $(addprefix $(MAK_DIR_GITWEB),$(GITWEB_PROGRAMS)) '$(DESTDIR_SQ)$(gitwebdir_SQ)'
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(gitwebstaticdir_SQ)'
	$(INSTALL) -m 644 $(addprefix $(MAK_DIR_GITWEB),$(GITWEB_FILES)) \
		'$(DESTDIR_SQ)$(gitwebstaticdir_SQ)'
ifndef NO_GITWEB
ifndef NO_PERL
install: install-gitweb
endif
endif

### Cleaning rules

.PHONY: gitweb-clean
gitweb-clean:
	$(RM) $(addprefix $(MAK_DIR_GITWEB),gitweb.cgi $(GITWEB_JS_IN) \
		$(GITWEB_JS_MIN) $(GITWEB_CSS_MIN) \
		GITWEB-BUILD-OPTIONS)
clean: gitweb-clean
