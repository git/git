### Remove GNU make implicit rules

## This speeds things up since we don't need to look for and stat() a
## "foo.c,v" every time a rule referring to "foo.c" is in play. See
## "make -p -f/dev/null | grep ^%::'".
%:: %,v
%:: RCS/%,v
%:: RCS/%
%:: s.%
%:: SCCS/s.%

## Likewise delete default $(SUFFIXES). See:
##
##     info make --index-search=.SUFFIXES
.SUFFIXES:

### Flags affecting all rules

# A GNU make extension since gmake 3.72 (released in late 1994) to
# remove the target of rules if commands in those rules fail. The
# default is to only do that if make itself receives a signal. Affects
# all targets, see:
#
#    info make --index-search=.DELETE_ON_ERROR
.DELETE_ON_ERROR:

### Global variables

## comma, empty, space: handy variables as these tokens are either
## special or can be hard to spot among other Makefile syntax.
comma := ,
empty :=
space := $(empty) $(empty)

### Quieting
## common
QUIET_SUBDIR0  = +$(MAKE) -C # space to separate -C and subdir
QUIET_SUBDIR1  =

ifneq ($(findstring w,$(firstword -$(MAKEFLAGS))),w)
PRINT_DIR = --no-print-directory
else # "make -w"
NO_SUBDIR = :
endif

ifneq ($(findstring s,$(firstword -$(MAKEFLAGS))),s)
ifndef V
## common
	QUIET_SUBDIR0  = +@subdir=
	QUIET_SUBDIR1  = ;$(NO_SUBDIR) echo '   ' SUBDIR $$subdir; \
			 $(MAKE) $(PRINT_DIR) -C $$subdir

	QUIET          = @
	QUIET_GEN      = @echo '   ' GEN $@;

	QUIET_MKDIR_P_PARENT  = @echo '   ' MKDIR -p $(@D);

## Used in "Makefile"
	QUIET_CC       = @echo '   ' CC $@;
	QUIET_AR       = @echo '   ' AR $@;
	QUIET_LINK     = @echo '   ' LINK $@;
	QUIET_BUILT_IN = @echo '   ' BUILTIN $@;
	QUIET_CP       = @echo '   ' CP $< $@;
	QUIET_LNCP     = @echo '   ' LN/CP $@;
	QUIET_XGETTEXT = @echo '   ' XGETTEXT $@;
	QUIET_MSGINIT  = @echo '   ' MSGINIT $@;
	QUIET_MSGFMT   = @echo '   ' MSGFMT $@;
	QUIET_MSGMERGE = @echo '   ' MSGMERGE $@;
	QUIET_GCOV     = @echo '   ' GCOV $@;
	QUIET_SP       = @echo '   ' SP $<;
	QUIET_HDR      = @echo '   ' HDR $(<:hcc=h);
	QUIET_RC       = @echo '   ' RC $@;

## Used in "Makefile": SPATCH
	QUIET_SPATCH			= @echo '   ' SPATCH $< \>$@;
	QUIET_SPATCH_TEST		= @echo '   ' SPATCH TEST $(@:.build/%=%);
	QUIET_SPATCH_CAT		= @echo '   ' SPATCH CAT $(@:%.patch=%.d/)\*\*.patch \>$@;

## Used in "Documentation/Makefile"
	QUIET_ASCIIDOC	= @echo '   ' ASCIIDOC $@;
	QUIET_XMLTO	= @echo '   ' XMLTO $@;
	QUIET_DB2TEXI	= @echo '   ' DB2TEXI $@;
	QUIET_MAKEINFO	= @echo '   ' MAKEINFO $@;
	QUIET_DBLATEX	= @echo '   ' DBLATEX $@;
	QUIET_XSLTPROC	= @echo '   ' XSLTPROC $@;
	QUIET_GEN	= @echo '   ' GEN $@;
	QUIET_STDERR	= 2> /dev/null

	QUIET_LINT_GITLINK	= @echo '   ' LINT GITLINK $<;
	QUIET_LINT_MANSEC	= @echo '   ' LINT MAN SEC $<;
	QUIET_LINT_MANEND	= @echo '   ' LINT MAN END $<;

	export V
endif
endif

### Templates

## mkdir_p_parent: lazily "mkdir -p" the path needed for a $@
## file. Uses $(wildcard) to avoid the "mkdir -p" if it's not
## needed.
##
## Is racy, but in a good way; we might redundantly (and safely)
## "mkdir -p" when running in parallel, but won't need to exhaustively create
## individual rules for "a" -> "prefix" -> "dir" -> "file" if given a
## "a/prefix/dir/file". This can instead be inserted at the start of
## the "a/prefix/dir/file" rule.
define mkdir_p_parent_template
$(if $(wildcard $(@D)),,$(QUIET_MKDIR_P_PARENT)$(shell mkdir -p $(@D)))
endef

## Getting sick of writing -L$(SOMELIBDIR) $(CC_LD_DYNPATH)$(SOMELIBDIR)?
## Write $(call libpath_template,$(SOMELIBDIR)) instead, perhaps?
## With CC_LD_DYNPATH set to either an empty string or to "-L", the
## the directory is not shown the second time.
define libpath_template
-L$(1) $(if $(filter-out -L,$(CC_LD_DYNPATH)),$(CC_LD_DYNPATH)$(1))
endef

# Populate build information into a file via GIT-VERSION-GEN. Requires the
# absolute path to the root source directory as well as input and output files
# as arguments, in that order.
define version_gen
GIT_BUILT_FROM_COMMIT="$(GIT_BUILT_FROM_COMMIT)" \
GIT_DATE="$(GIT_DATE)" \
GIT_USER_AGENT="$(GIT_USER_AGENT)" \
GIT_VERSION="$(GIT_VERSION_OVERRIDE)" \
$(SHELL_PATH) "$(1)/GIT-VERSION-GEN" "$(1)" "$(2)" "$(3)"
endef
