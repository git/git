#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

## Definitions of private macros.

# GIT_CONF_SUBST(VAL, VAR)
# ------------------------
# Cause the line "VAR=VAL" to be eventually appended to ${config_file}.
AC_DEFUN([GIT_CONF_SUBST],
[AC_REQUIRE([GIT_CONF_SUBST_INIT])
config_appended_defs="$config_appended_defs${newline}dnl
$1=m4_if([$#],[1],[${$1}],[$2])"])

# GIT_CONF_SUBST_INIT
# -------------------
# Prepare shell variables and autoconf machine required by later calls
# to GIT_CONF_SUBST.
AC_DEFUN([GIT_CONF_SUBST_INIT],
    [config_appended_defs=; newline='
'
    AC_CONFIG_COMMANDS([$config_file],
                       [echo "$config_appended_defs" >> "$config_file"],
                       [config_file=$config_file
                        config_appended_defs="$config_appended_defs"])])

# GIT_ARG_SET_PATH(PROGRAM)
# -------------------------
# Provide --with-PROGRAM=PATH option to set PATH to PROGRAM
# Optional second argument allows setting NO_PROGRAM=YesPlease if
# --without-PROGRAM version used.
AC_DEFUN([GIT_ARG_SET_PATH],
    [AC_ARG_WITH([$1],
        [AS_HELP_STRING([--with-$1=PATH],
                        [provide PATH to $1])],
        [GIT_CONF_APPEND_PATH([$1], [$2])],
        [])])

# GIT_CONF_APPEND_PATH(PROGRAM)
# -----------------------------
# Parse --with-PROGRAM=PATH option to set PROGRAM_PATH=PATH
# Used by GIT_ARG_SET_PATH(PROGRAM)
# Optional second argument allows setting NO_PROGRAM=YesPlease if
# --without-PROGRAM is used.
AC_DEFUN([GIT_CONF_APPEND_PATH],
    [m4_pushdef([GIT_UC_PROGRAM], m4_toupper([$1]))dnl
    if test "$withval" = "no"; then
	if test -n "$2"; then
		GIT_UC_PROGRAM[]_PATH=$withval
		AC_MSG_NOTICE([Disabling use of GIT_UC_PROGRAM])
		GIT_CONF_SUBST([NO_]GIT_UC_PROGRAM, [YesPlease])
		GIT_CONF_SUBST(GIT_UC_PROGRAM[]_PATH, [])
	else
		AC_MSG_ERROR([You cannot use git without $1])
	fi
    else
	if test "$withval" = "yes"; then
		AC_MSG_WARN([You should provide path for --with-$1=PATH])
	else
		GIT_UC_PROGRAM[]_PATH=$withval
		AC_MSG_NOTICE([Setting GIT_UC_PROGRAM[]_PATH to $withval])
		GIT_CONF_SUBST(GIT_UC_PROGRAM[]_PATH, [$withval])
	fi
    fi
    m4_popdef([GIT_UC_PROGRAM])])

# GIT_PARSE_WITH(PACKAGE)
# -----------------------
# For use in AC_ARG_WITH action-if-found, for packages default ON.
# * Set NO_PACKAGE=YesPlease for --without-PACKAGE
# * Set PACKAGEDIR=PATH for --with-PACKAGE=PATH
# * Unset NO_PACKAGE for --with-PACKAGE without ARG
AC_DEFUN([GIT_PARSE_WITH],
    [m4_pushdef([GIT_UC_PACKAGE], m4_toupper([$1]))dnl
    if test "$withval" = "no"; then
	NO_[]GIT_UC_PACKAGE=YesPlease
    elif test "$withval" = "yes"; then
	NO_[]GIT_UC_PACKAGE=
    else
	NO_[]GIT_UC_PACKAGE=
	GIT_UC_PACKAGE[]DIR=$withval
	AC_MSG_NOTICE([Setting GIT_UC_PACKAGE[]DIR to $withval])
	GIT_CONF_SUBST(GIT_UC_PACKAGE[DIR], [$withval])
    fi
    m4_popdef([GIT_UC_PACKAGE])])

# GIT_PARSE_WITH_SET_MAKE_VAR(WITHNAME, VAR, HELP_TEXT)
# -----------------------------------------------------
# Set VAR to the value specified by --with-WITHNAME.
# No verification of arguments is performed, but warnings are issued
# if either 'yes' or 'no' is specified.
# HELP_TEXT is presented when --help is called.
# This is a direct way to allow setting variables in the Makefile.
AC_DEFUN([GIT_PARSE_WITH_SET_MAKE_VAR],
[AC_ARG_WITH([$1],
 [AS_HELP_STRING([--with-$1=VALUE], $3)],
 if test -n "$withval"; then
  if test "$withval" = "yes" -o "$withval" = "no"; then
    AC_MSG_WARN([You likely do not want either 'yes' or 'no' as]
		     [a value for $1 ($2).  Maybe you do...?])
  fi
  AC_MSG_NOTICE([Setting $2 to $withval])
  GIT_CONF_SUBST([$2], [$withval])
 fi)])# GIT_PARSE_WITH_SET_MAKE_VAR

#
# GIT_CHECK_FUNC(FUNCTION, IFTRUE, IFFALSE)
# -----------------------------------------
# Similar to AC_CHECK_FUNC, but on systems that do not generate
# warnings for missing prototypes (e.g. FreeBSD when compiling without
# -Wall), it does not work.  By looking for function definition in
# libraries, this problem can be worked around.
AC_DEFUN([GIT_CHECK_FUNC],[AC_CHECK_FUNC([$1],[
  AC_SEARCH_LIBS([$1],,
  [$2],[$3])
],[$3])])

#
# GIT_STASH_FLAGS(BASEPATH_VAR)
# -----------------------------
# Allow for easy stashing of LDFLAGS and CPPFLAGS before running
# tests that may want to take user settings into account.
AC_DEFUN([GIT_STASH_FLAGS],[
if test -n "$1"; then
   old_CPPFLAGS="$CPPFLAGS"
   old_LDFLAGS="$LDFLAGS"
   CPPFLAGS="-I$1/include $CPPFLAGS"
   LDFLAGS="-L$1/$lib $LDFLAGS"
fi
])

dnl
dnl GIT_UNSTASH_FLAGS(BASEPATH_VAR)
dnl -----------------------------
dnl Restore the stashed *FLAGS values.
AC_DEFUN([GIT_UNSTASH_FLAGS],[
if test -n "$1"; then
   CPPFLAGS="$old_CPPFLAGS"
   LDFLAGS="$old_LDFLAGS"
fi
])

## Configure body starts here.

AC_PREREQ(2.59)
AC_INIT([git], [@@GIT_VERSION@@], [git@vger.kernel.org])

AC_CONFIG_SRCDIR([git.c])

config_file=config.mak.autogen
config_in=config.mak.in

GIT_CONF_SUBST([AUTOCONFIGURED], [YesPlease])

# Directories holding "saner" versions of common or POSIX binaries.
AC_ARG_WITH([sane-tool-path],
  [AS_HELP_STRING(
    [--with-sane-tool-path=DIR-1[[:DIR-2...:DIR-n]]],
    [Directories to prepend to PATH in build system and generated scripts])],
  [if test "$withval" = "no"; then
    withval=''
  else
    AC_MSG_NOTICE([Setting SANE_TOOL_PATH to '$withval'])
  fi
  GIT_CONF_SUBST([SANE_TOOL_PATH], [$withval])],
  [# If the "--with-sane-tool-path" option was not given, don't touch
   # SANE_TOOL_PATH here, but let defaults in Makefile take care of it.
   # This should minimize spurious differences in the behaviour of the
   # Git build system when configure is used w.r.t. when it is not.
   :])

## Site configuration related to programs (before tests)
## --with-PACKAGE[=ARG] and --without-PACKAGE
#
# Set lib to alternative name of lib directory (e.g. lib64)
AC_ARG_WITH([lib],
 [AS_HELP_STRING([--with-lib=ARG],
                 [ARG specifies alternative name for lib directory])],
 [if test "$withval" = "no" || test "$withval" = "yes"; then
	AC_MSG_WARN([You should provide name for --with-lib=ARG])
  else
	lib=$withval
	AC_MSG_NOTICE([Setting lib to '$lib'])
	GIT_CONF_SUBST([lib])
  fi])

if test -z "$lib"; then
   AC_MSG_NOTICE([Setting lib to 'lib' (the default)])
   lib=lib
fi

AC_ARG_ENABLE([pthreads],
 [AS_HELP_STRING([--enable-pthreads=FLAGS],
  [FLAGS is the value to pass to the compiler to enable POSIX Threads.]
  [The default if FLAGS is not specified is to try first -pthread]
  [and then -lpthread.]
  [--disable-pthreads will disable threading.])],
[
if test "x$enableval" = "xyes"; then
   AC_MSG_NOTICE([Will try -pthread then -lpthread to enable POSIX Threads])
elif test "x$enableval" != "xno"; then
   PTHREAD_CFLAGS=$enableval
   AC_MSG_NOTICE([Setting '$PTHREAD_CFLAGS' as the FLAGS to enable POSIX Threads])
else
   AC_MSG_NOTICE([POSIX Threads will be disabled.])
   NO_PTHREADS=YesPlease
   USER_NOPTHREAD=1
fi],
[
   AC_MSG_NOTICE([Will try -pthread then -lpthread to enable POSIX Threads.])
])

# Define option to enable JavaScript minification
AC_ARG_ENABLE([jsmin],
[AS_HELP_STRING([--enable-jsmin=PATH],
  [PATH is the name of a JavaScript minifier or the absolute path to one.])],
[
  JSMIN=$enableval;
  AC_MSG_NOTICE([Setting JSMIN to '$JSMIN' to enable JavaScript minifying])
  GIT_CONF_SUBST([JSMIN])
])

# Define option to enable CSS minification
AC_ARG_ENABLE([cssmin],
[AS_HELP_STRING([--enable-cssmin=PATH],
  [PATH is the name of a CSS minifier or the absolute path to one.])],
[
  CSSMIN=$enableval;
  AC_MSG_NOTICE([Setting CSSMIN to '$CSSMIN' to enable CSS minifying])
  GIT_CONF_SUBST([CSSMIN])
])

## Site configuration (override autodetection)
## --with-PACKAGE[=ARG] and --without-PACKAGE
AC_MSG_NOTICE([CHECKS for site configuration])
#
# Define NO_SVN_TESTS if you want to skip time-consuming SVN interoperability
# tests.  These tests take up a significant amount of the total test time
# but are not needed unless you plan to talk to SVN repos.
#
# Define PPC_SHA1 environment variable when running make to make use of
# a bundled SHA1 routine optimized for PowerPC.
#
# Define NO_OPENSSL environment variable if you do not have OpenSSL.
#
# Define OPENSSLDIR=/foo/bar if your openssl header and library files are in
# /foo/bar/include and /foo/bar/lib directories.
AC_ARG_WITH(openssl,
AS_HELP_STRING([--with-openssl],[use OpenSSL library (default is YES)])
AS_HELP_STRING([],              [ARG can be prefix for openssl library and headers]),
GIT_PARSE_WITH([openssl]))

# Define USE_LIBPCRE if you have and want to use libpcre. Various
# commands such as log and grep offer runtime options to use
# Perl-compatible regular expressions instead of standard or extended
# POSIX regular expressions. Only libpcre version 2 is supported.
#
# Define LIBPCREDIR=/foo/bar if your PCRE header and library files are in
# /foo/bar/include and /foo/bar/lib directories.
#

AC_ARG_WITH(libpcre1,
AS_HELP_STRING([--with-libpcre1],[DEPRECATED]),
    AC_MSG_ERROR([support for --with-libpcre1 has been removed. Use --with-libpcre2!])
    )

AC_ARG_WITH(libpcre,
AS_HELP_STRING([--with-libpcre],[synonym for --with-libpcre2]),
    if test "$withval" = "no"; then
	USE_LIBPCRE2=
    elif test "$withval" = "yes"; then
	USE_LIBPCRE2=YesPlease
    else
	USE_LIBPCRE2=YesPlease
	LIBPCREDIR=$withval
	AC_MSG_NOTICE([Setting LIBPCREDIR to $LIBPCREDIR])
        dnl USE_LIBPCRE2 can still be modified below, so don't substitute
        dnl it yet.
	GIT_CONF_SUBST([LIBPCREDIR])
    fi)

AC_ARG_WITH(libpcre2,
AS_HELP_STRING([--with-libpcre2],[support Perl-compatible regexes via libpcre2 (default is NO)])
AS_HELP_STRING([],           [ARG can be also prefix for libpcre library and headers]),
    if test -n "$USE_LIBPCRE2"; then
        AC_MSG_ERROR([Only supply one of --with-libpcre or its synonym --with-libpcre2!])
    fi

    if test "$withval" = "no"; then
	USE_LIBPCRE2=
    elif test "$withval" = "yes"; then
	USE_LIBPCRE2=YesPlease
    else
	USE_LIBPCRE2=YesPlease
	LIBPCREDIR=$withval
	AC_MSG_NOTICE([Setting LIBPCREDIR to $LIBPCREDIR])
        dnl USE_LIBPCRE2 can still be modified below, so don't substitute
        dnl it yet.
	GIT_CONF_SUBST([LIBPCREDIR])
    fi)
#
# Define HAVE_ALLOCA_H if you have working alloca(3) defined in that header.
AC_FUNC_ALLOCA
case $ac_cv_working_alloca_h in
    yes)    HAVE_ALLOCA_H=YesPlease;;
    *)      HAVE_ALLOCA_H='';;
esac
GIT_CONF_SUBST([HAVE_ALLOCA_H])
#
# Define NO_CURL if you do not have curl installed.  git-http-pull and
# git-http-push are not built, and you cannot use http:// and https://
# transports.
#
# Define CURLDIR=/foo/bar if your curl header and library files are in
# /foo/bar/include and /foo/bar/lib directories.
AC_ARG_WITH(curl,
AS_HELP_STRING([--with-curl],[support http(s):// transports (default is YES)])
AS_HELP_STRING([],           [ARG can be also prefix for curl library and headers]),
GIT_PARSE_WITH(curl))
#
# Define NO_EXPAT if you do not have expat installed.  git-http-push is
# not built, and you cannot push using http:// and https:// transports.
#
# Define EXPATDIR=/foo/bar if your expat header and library files are in
# /foo/bar/include and /foo/bar/lib directories.
AC_ARG_WITH(expat,
AS_HELP_STRING([--with-expat],
[support git-push using http:// and https:// transports via WebDAV (default is YES)])
AS_HELP_STRING([],            [ARG can be also prefix for expat library and headers]),
GIT_PARSE_WITH(expat))
#
# Define NO_FINK if you are building on Darwin/Mac OS X, have Fink
# installed in /sw, but don't want GIT to link against any libraries
# installed there.  If defined you may specify your own (or Fink's)
# include directories and library directories by defining CFLAGS
# and LDFLAGS appropriately.
#
# Define NO_DARWIN_PORTS if you are building on Darwin/Mac OS X,
# have DarwinPorts installed in /opt/local, but don't want GIT to
# link against any libraries installed there.  If defined you may
# specify your own (or DarwinPort's) include directories and
# library directories by defining CFLAGS and LDFLAGS appropriately.
#
# Define NO_MMAP if you want to avoid mmap.
#
# Define NO_ICONV if your libc does not properly support iconv.
AC_ARG_WITH(iconv,
AS_HELP_STRING([--without-iconv],
[if your architecture doesn't properly support iconv])
AS_HELP_STRING([--with-iconv=PATH],
[PATH is prefix for libiconv library and headers])
AS_HELP_STRING([],
[used only if you need linking with libiconv]),
GIT_PARSE_WITH(iconv))

## --enable-FEATURE[=ARG] and --disable-FEATURE
#
# Define USE_NSEC below if you want git to care about sub-second file mtimes
# and ctimes. Note that you need recent glibc (at least 2.2.4) for this, and
# it will BREAK YOUR LOCAL DIFFS! show-diff and anything using it will likely
# randomly break unless your underlying filesystem supports those sub-second
# times (my ext3 doesn't).
#
# Define USE_STDEV below if you want git to care about the underlying device
# change being considered an inode change from the update-index perspective.

#
# Allow user to set ETC_GITCONFIG variable
GIT_PARSE_WITH_SET_MAKE_VAR(gitconfig, ETC_GITCONFIG,
			Use VALUE instead of /etc/gitconfig as the
			global git configuration file.
			If VALUE is not fully qualified it will be interpreted
			as a path relative to the computed prefix at runtime.)

#
# Allow user to set ETC_GITATTRIBUTES variable
GIT_PARSE_WITH_SET_MAKE_VAR(gitattributes, ETC_GITATTRIBUTES,
			Use VALUE instead of /etc/gitattributes as the
			global git attributes file.
			If VALUE is not fully qualified it will be interpreted
			as a path relative to the computed prefix at runtime.)

#
# Allow user to set the default pager
GIT_PARSE_WITH_SET_MAKE_VAR(pager, DEFAULT_PAGER,
			Use VALUE as the fall-back pager instead of 'less'.
			This is used by things like 'git log' when the user
			does not specify a pager to use through alternate
			methods. eg: /usr/bin/pager)
#
# Allow user to set the default editor
GIT_PARSE_WITH_SET_MAKE_VAR(editor, DEFAULT_EDITOR,
			Use VALUE as the fall-back editor instead of 'vi'.
			This is used by things like 'git commit' when the user
			does not specify a preferred editor through other
			methods. eg: /usr/bin/editor)

#
# Define SHELL_PATH to provide path to shell.
GIT_ARG_SET_PATH(shell)
#
# Define PERL_PATH to provide path to Perl.
GIT_ARG_SET_PATH(perl)
#
# Define PYTHON_PATH to provide path to Python.
GIT_ARG_SET_PATH(python, allow-without)
#
# Define ZLIB_PATH to provide path to zlib.
GIT_ARG_SET_PATH(zlib)
#
# Declare the with-tcltk/without-tcltk options.
AC_ARG_WITH(tcltk,
AS_HELP_STRING([--with-tcltk],[use Tcl/Tk GUI (default is YES)])
AS_HELP_STRING([],[ARG is the full path to the Tcl/Tk interpreter.])
AS_HELP_STRING([],[Bare --with-tcltk will make the GUI part only if])
AS_HELP_STRING([],[Tcl/Tk interpreter will be found in a system.]),
GIT_PARSE_WITH(tcltk))
#


## Checks for programs.
AC_MSG_NOTICE([CHECKS for programs])
#
AC_PROG_CC([cc gcc])
AC_C_INLINE
case $ac_cv_c_inline in
  inline | yes | no) INLINE='';;
  *)                 INLINE=$ac_cv_c_inline ;;
esac
GIT_CONF_SUBST([INLINE])

# which switch to pass runtime path to dynamic libraries to the linker
AC_CACHE_CHECK([if linker supports -R], git_cv_ld_dashr, [
   SAVE_LDFLAGS="${LDFLAGS}"
   LDFLAGS="${SAVE_LDFLAGS} -R /"
   AC_LINK_IFELSE([AC_LANG_PROGRAM([], [])], [git_cv_ld_dashr=yes], [git_cv_ld_dashr=no])
   LDFLAGS="${SAVE_LDFLAGS}"
])
if test "$git_cv_ld_dashr" = "yes"; then
   CC_LD_DYNPATH=-R
else
   AC_CACHE_CHECK([if linker supports -Wl,-rpath,], git_cv_ld_wl_rpath, [
      SAVE_LDFLAGS="${LDFLAGS}"
      LDFLAGS="${SAVE_LDFLAGS} -Wl,-rpath,/"
      AC_LINK_IFELSE([AC_LANG_PROGRAM([], [])], [git_cv_ld_wl_rpath=yes], [git_cv_ld_wl_rpath=no])
      LDFLAGS="${SAVE_LDFLAGS}"
   ])
   if test "$git_cv_ld_wl_rpath" = "yes"; then
      CC_LD_DYNPATH=-Wl,-rpath,
   else
      AC_CACHE_CHECK([if linker supports -rpath], git_cv_ld_rpath, [
         SAVE_LDFLAGS="${LDFLAGS}"
         LDFLAGS="${SAVE_LDFLAGS} -rpath /"
         AC_LINK_IFELSE([AC_LANG_PROGRAM([], [])], [git_cv_ld_rpath=yes], [git_cv_ld_rpath=no])
         LDFLAGS="${SAVE_LDFLAGS}"
      ])
      if test "$git_cv_ld_rpath" = "yes"; then
         CC_LD_DYNPATH=-rpath
      else
         AC_CACHE_CHECK([if linker supports -Wl,+b,], git_cv_ld_wl_b, [
            SAVE_LDFLAGS="${LDFLAGS}"
            LDFLAGS="${SAVE_LDFLAGS} -Wl,+b,/"
            AC_LINK_IFELSE([AC_LANG_PROGRAM([], [])], [git_cv_ld_wl_b=yes], [git_cv_ld_wl_b=no])
            LDFLAGS="${SAVE_LDFLAGS}"
         ])
         if test "$git_cv_ld_wl_b" = "yes"; then
            CC_LD_DYNPATH=-Wl,+b,
          else
             CC_LD_DYNPATH=
             AC_MSG_WARN([linker does not support runtime path to dynamic libraries])
          fi
      fi
   fi
fi
GIT_CONF_SUBST([CC_LD_DYNPATH])
#AC_PROG_INSTALL		# needs install-sh or install.sh in sources
AC_CHECK_TOOLS(AR, [gar ar], :)
AC_CHECK_PROGS(TAR, [gtar tar])
AC_CHECK_PROGS(DIFF, [gnudiff gdiff diff])
# TCLTK_PATH will be set to some value if we want Tcl/Tk
# or will be empty otherwise.
if test -n "$NO_TCLTK"; then
  TCLTK_PATH=
else
  if test "$with_tcltk" = ""; then
  # No Tcl/Tk switches given. Do not check for Tcl/Tk, use bare 'wish'.
    TCLTK_PATH=wish
  elif test "$with_tcltk" = "yes"; then
  # Tcl/Tk check requested.
    AC_CHECK_PROGS(TCLTK_PATH, [wish], )
  else
    AC_MSG_RESULT([Using Tcl/Tk interpreter $with_tcltk])
    TCLTK_PATH="$with_tcltk"
  fi
fi
GIT_CONF_SUBST([TCLTK_PATH])
AC_CHECK_PROGS(ASCIIDOC, [asciidoc])
if test -n "$ASCIIDOC"; then
	AC_MSG_CHECKING([for asciidoc version])
	asciidoc_version=`$ASCIIDOC --version 2>/dev/null`
	case "${asciidoc_version}" in
	asciidoc' '8*)
		AC_MSG_RESULT([${asciidoc_version}])
		;;
	*)
		AC_MSG_RESULT([${asciidoc_version} (unknown)])
		;;
	esac
fi

if grep -a ascii configure.ac >/dev/null; then
  AC_MSG_RESULT([Using 'grep -a' for sane_grep])
  SANE_TEXT_GREP=-a
else
  SANE_TEXT_GREP=
fi
GIT_CONF_SUBST([SANE_TEXT_GREP])

## Checks for libraries.
AC_MSG_NOTICE([CHECKS for libraries])
#
# Define NO_OPENSSL environment variable if you do not have OpenSSL.
# Define NEEDS_SSL_WITH_CRYPTO if you need -lcrypto with -lssl (Darwin).

GIT_STASH_FLAGS($OPENSSLDIR)

AC_CHECK_LIB([crypto], [SHA1_Init],
[NEEDS_SSL_WITH_CRYPTO=],
[AC_CHECK_LIB([ssl], [SHA1_Init],
 [NEEDS_SSL_WITH_CRYPTO=YesPlease NO_OPENSSL=],
 [NEEDS_SSL_WITH_CRYPTO=          NO_OPENSSL=YesPlease])])

GIT_UNSTASH_FLAGS($OPENSSLDIR)

GIT_CONF_SUBST([NEEDS_SSL_WITH_CRYPTO])
GIT_CONF_SUBST([NO_OPENSSL])

#
# Handle the USE_LIBPCRE options potentially set above.
#

if test -n "$USE_LIBPCRE2"; then

GIT_STASH_FLAGS($LIBPCREDIR)

AC_CHECK_LIB([pcre2-8], [pcre2_config_8],
[USE_LIBPCRE2=YesPlease],
[USE_LIBPCRE2=])

GIT_UNSTASH_FLAGS($LIBPCREDIR)

GIT_CONF_SUBST([USE_LIBPCRE2])

fi

#
# Define NO_CURL if you do not have libcurl installed.  git-http-pull and
# git-http-push are not built, and you cannot use http:// and https://
# transports.

GIT_STASH_FLAGS($CURLDIR)

AC_CHECK_LIB([curl], [curl_global_init],
[NO_CURL=],
[NO_CURL=YesPlease])

GIT_UNSTASH_FLAGS($CURLDIR)

GIT_CONF_SUBST([NO_CURL])

if test -z "$NO_CURL"; then

AC_CHECK_PROG([CURL_CONFIG], [curl-config],
[curl-config],
[no])

if test $CURL_CONFIG != no; then
    GIT_CONF_SUBST([CURL_CONFIG])

    if test -z "$CURL_CONFIG_OPTS"; then
        CURL_CONFIG_OPTS="--libs"
    fi

    CURL_LDFLAGS=$($CURL_CONFIG $CURL_CONFIG_OPTS)
    AC_MSG_NOTICE([Setting CURL_LDFLAGS to '$CURL_LDFLAGS'])
    GIT_CONF_SUBST([CURL_LDFLAGS], [$CURL_LDFLAGS])
fi

fi


#
# Define NO_EXPAT if you do not have expat installed.  git-http-push is
# not built, and you cannot push using http:// and https:// transports.

GIT_STASH_FLAGS($EXPATDIR)

AC_CHECK_LIB([expat], [XML_ParserCreate],
[NO_EXPAT=],
[NO_EXPAT=YesPlease])

GIT_UNSTASH_FLAGS($EXPATDIR)

GIT_CONF_SUBST([NO_EXPAT])

#
# Define NEEDS_LIBICONV if linking with libc is not enough (Darwin and
# some Solaris installations).
# Define NO_ICONV if neither libc nor libiconv support iconv.

if test -z "$NO_ICONV"; then

GIT_STASH_FLAGS($ICONVDIR)

AC_DEFUN([ICONVTEST_SRC],
[AC_LANG_PROGRAM([#include <iconv.h>],
 [iconv_open("", "");])])

if test -n "$ICONVDIR"; then
   lib_order="-liconv -lc"
else
   lib_order="-lc -liconv"
fi

NO_ICONV=YesPlease

for l in $lib_order; do
    if test "$l" = "-liconv"; then
       NEEDS_LIBICONV=YesPlease
    else
       NEEDS_LIBICONV=
    fi

    old_LIBS="$LIBS"
    LIBS="$LIBS $l"
    AC_MSG_CHECKING([for iconv in $l])
    AC_LINK_IFELSE([ICONVTEST_SRC],
	[AC_MSG_RESULT([yes])
	NO_ICONV=
	break],
	[AC_MSG_RESULT([no])])
    LIBS="$old_LIBS"
done

#in case of break
LIBS="$old_LIBS"

GIT_UNSTASH_FLAGS($ICONVDIR)

GIT_CONF_SUBST([NEEDS_LIBICONV])
GIT_CONF_SUBST([NO_ICONV])

if test -n "$NO_ICONV"; then
    NEEDS_LIBICONV=
fi

fi

#
# Define NO_DEFLATE_BOUND if deflateBound is missing from zlib.

GIT_STASH_FLAGS($ZLIB_PATH)

AC_DEFUN([ZLIBTEST_SRC], [
AC_LANG_PROGRAM([#include <zlib.h>],
 [deflateBound(0, 0);])])
AC_MSG_CHECKING([for deflateBound in -lz])
old_LIBS="$LIBS"
LIBS="$LIBS -lz"
AC_LINK_IFELSE([ZLIBTEST_SRC],
	[AC_MSG_RESULT([yes])],
	[AC_MSG_RESULT([no])
	NO_DEFLATE_BOUND=yes])
LIBS="$old_LIBS"

GIT_UNSTASH_FLAGS($ZLIB_PATH)

GIT_CONF_SUBST([NO_DEFLATE_BOUND])

#
# Define NEEDS_SOCKET if linking with libc is not enough (SunOS,
# Patrick Mauritz).
AC_CHECK_LIB([c], [socket],
[NEEDS_SOCKET=],
[NEEDS_SOCKET=YesPlease])
GIT_CONF_SUBST([NEEDS_SOCKET])
test -n "$NEEDS_SOCKET" && LIBS="$LIBS -lsocket"

#
# The next few tests will define NEEDS_RESOLV if linking with
# libresolv provides some of the functions we would normally get
# from libc.
NEEDS_RESOLV=
#
# Define NO_INET_NTOP if linking with -lresolv is not enough.
# Solaris 2.7 in particular hos inet_ntop in -lresolv.
NO_INET_NTOP=
AC_CHECK_FUNC([inet_ntop],
    [],
    [AC_CHECK_LIB([resolv], [inet_ntop],
	[NEEDS_RESOLV=YesPlease],
	[NO_INET_NTOP=YesPlease])
])
GIT_CONF_SUBST([NO_INET_NTOP])
#
# Define NO_INET_PTON if linking with -lresolv is not enough.
# Solaris 2.7 in particular hos inet_pton in -lresolv.
NO_INET_PTON=
AC_CHECK_FUNC([inet_pton],
    [],
    [AC_CHECK_LIB([resolv], [inet_pton],
	[NEEDS_RESOLV=YesPlease],
	[NO_INET_PTON=YesPlease])
])
GIT_CONF_SUBST([NO_INET_PTON])
#
# Define NO_HSTRERROR if linking with -lresolv is not enough.
# Solaris 2.6 in particular has no hstrerror, even in -lresolv.
NO_HSTRERROR=
AC_CHECK_FUNC([hstrerror],
    [],
    [AC_CHECK_LIB([resolv], [hstrerror],
	[NEEDS_RESOLV=YesPlease],
	[NO_HSTRERROR=YesPlease])
])
GIT_CONF_SUBST([NO_HSTRERROR])

dnl This must go after all the possible places for its initialization,
dnl in the AC_CHECK_FUNC invocations above.
GIT_CONF_SUBST([NEEDS_RESOLV])
#
# If any of the above tests determined that -lresolv is needed at
# build-time, also set it here for remaining configure-time checks.
test -n "$NEEDS_RESOLV" && LIBS="$LIBS -lresolv"

AC_CHECK_LIB([c], [basename],
[NEEDS_LIBGEN=],
[NEEDS_LIBGEN=YesPlease])
GIT_CONF_SUBST([NEEDS_LIBGEN])
test -n "$NEEDS_LIBGEN" && LIBS="$LIBS -lgen"

AC_DEFUN([LIBINTL_SRC], [
AC_LANG_PROGRAM([[
#include <libintl.h>
]],[[
char *msg = gettext("test");
]])])

AC_MSG_CHECKING([if libc contains libintl])
AC_LINK_IFELSE([LIBINTL_SRC],
	[AC_MSG_RESULT([yes])
	LIBC_CONTAINS_LIBINTL=YesPlease],
	[AC_MSG_RESULT([no])
	LIBC_CONTAINS_LIBINTL=])
GIT_CONF_SUBST([LIBC_CONTAINS_LIBINTL])

#
# Define NO_GETTEXT if you don't want Git output to be translated.
# A translated Git requires GNU libintl or another gettext implementation
AC_CHECK_HEADER([libintl.h],
[NO_GETTEXT=],
[NO_GETTEXT=YesPlease])
GIT_CONF_SUBST([NO_GETTEXT])

if test -z "$NO_GETTEXT"; then
    test -n "$LIBC_CONTAINS_LIBINTL" || LIBS="$LIBS -lintl"
fi

## Checks for header files.
AC_MSG_NOTICE([CHECKS for header files])
#
# Define NO_SYS_SELECT_H if you don't have sys/select.h.
AC_CHECK_HEADER([sys/select.h],
[NO_SYS_SELECT_H=],
[NO_SYS_SELECT_H=UnfortunatelyYes])
GIT_CONF_SUBST([NO_SYS_SELECT_H])
#
# Define NO_POLL_H if you don't have poll.h
AC_CHECK_HEADER([poll.h],
[NO_POLL_H=],
[NO_POLL_H=UnfortunatelyYes])
GIT_CONF_SUBST([NO_POLL_H])
#
# Define NO_SYS_POLL_H if you don't have sys/poll.h
AC_CHECK_HEADER([sys/poll.h],
[NO_SYS_POLL_H=],
[NO_SYS_POLL_H=UnfortunatelyYes])
GIT_CONF_SUBST([NO_SYS_POLL_H])
#
# Define NO_INTTYPES_H if you don't have inttypes.h
AC_CHECK_HEADER([inttypes.h],
[NO_INTTYPES_H=],
[NO_INTTYPES_H=UnfortunatelyYes])
GIT_CONF_SUBST([NO_INTTYPES_H])
#
# Define OLD_ICONV if your library has an old iconv(), where the second
# (input buffer pointer) parameter is declared with type (const char **).
AC_DEFUN([OLDICONVTEST_SRC], [
AC_LANG_PROGRAM([[
#include <iconv.h>

extern size_t iconv(iconv_t cd,
		    char **inbuf, size_t *inbytesleft,
		    char **outbuf, size_t *outbytesleft);
]], [])])

GIT_STASH_FLAGS($ICONVDIR)

AC_MSG_CHECKING([for old iconv()])
AC_COMPILE_IFELSE([OLDICONVTEST_SRC],
	[AC_MSG_RESULT([no])],
	[AC_MSG_RESULT([yes])
	AC_DEFINE(HAVE_OLD_ICONV, 1)
	OLD_ICONV=UnfortunatelyYes])

GIT_UNSTASH_FLAGS($ICONVDIR)

GIT_CONF_SUBST([OLD_ICONV])

#
# Define ICONV_OMITS_BOM if you are on a system which
# iconv omits bom for utf-{16,32}
if test -z "$NO_ICONV"; then
AC_CACHE_CHECK([whether iconv omits bom for utf-16 and utf-32],
 [ac_cv_iconv_omits_bom],
[
old_LIBS="$LIBS"
if test -n "$NEEDS_LIBICONV"; then
	LIBS="$LIBS -liconv"
fi

AC_RUN_IFELSE(
	[AC_LANG_PROGRAM([AC_INCLUDES_DEFAULT
	#include <iconv.h>
	#ifdef HAVE_OLD_ICONV
	typedef const char *iconv_ibp;
	#else
	typedef char *iconv_ibp;
	#endif
	],
	[[
	int v;
	iconv_t conv;
	char in[] = "a"; iconv_ibp pin = in;
	char out[20] = ""; char *pout = out;
	size_t isz = sizeof in;
	size_t osz = sizeof out;

	conv = iconv_open("UTF-16", "UTF-8");
	iconv(conv, &pin, &isz, &pout, &osz);
	iconv_close(conv);
	v = (unsigned char)(out[0]) + (unsigned char)(out[1]);
	return v != 0xfe + 0xff;
	]])],
	[ac_cv_iconv_omits_bom=no],
	[ac_cv_iconv_omits_bom=yes])

LIBS="$old_LIBS"
])
if test "x$ac_cv_iconv_omits_bom" = xyes; then
	ICONV_OMITS_BOM=Yes
else
	ICONV_OMITS_BOM=
fi
GIT_CONF_SUBST([ICONV_OMITS_BOM])
fi

## Checks for typedefs, structures, and compiler characteristics.
AC_MSG_NOTICE([CHECKS for typedefs, structures, and compiler characteristics])
#
TYPE_SOCKLEN_T
case $ac_cv_type_socklen_t in
  yes)	SOCKLEN_T='';;
  *)  	SOCKLEN_T=$git_cv_socklen_t_equiv;;
esac
GIT_CONF_SUBST([SOCKLEN_T])

#
# Define NO_STRUCT_ITIMERVAL if you don't have struct itimerval.
AC_CHECK_TYPES([struct itimerval],
[NO_STRUCT_ITIMERVAL=],
[NO_STRUCT_ITIMERVAL=UnfortunatelyYes],
[#include <sys/time.h>])
GIT_CONF_SUBST([NO_STRUCT_ITIMERVAL])
#
# Define USE_ST_TIMESPEC=YesPlease when stat.st_mtimespec.tv_nsec exists.
# Define NO_NSEC=YesPlease when neither stat.st_mtim.tv_nsec nor
# stat.st_mtimespec.tv_nsec exists.
AC_CHECK_MEMBER([struct stat.st_mtimespec.tv_nsec])
AC_CHECK_MEMBER([struct stat.st_mtim.tv_nsec])
if test x$ac_cv_member_struct_stat_st_mtimespec_tv_nsec = xyes; then
	USE_ST_TIMESPEC=YesPlease
	GIT_CONF_SUBST([USE_ST_TIMESPEC])
elif test x$ac_cv_member_struct_stat_st_mtim_tv_nsec != xyes; then
	NO_NSEC=YesPlease
	GIT_CONF_SUBST([NO_NSEC])
fi
#
# Define NO_D_TYPE_IN_DIRENT if your platform defines DT_UNKNOWN but lacks
# d_type in struct dirent (latest Cygwin -- will be fixed soonish).
AC_CHECK_MEMBER(struct dirent.d_type,
[NO_D_TYPE_IN_DIRENT=],
[NO_D_TYPE_IN_DIRENT=YesPlease],
[#include <dirent.h>])
GIT_CONF_SUBST([NO_D_TYPE_IN_DIRENT])
#
# Define NO_GECOS_IN_PWENT if you don't have pw_gecos in struct passwd
# in the C library.
AC_CHECK_MEMBER(struct passwd.pw_gecos,
[NO_GECOS_IN_PWENT=],
[NO_GECOS_IN_PWENT=YesPlease],
[#include <pwd.h>])
GIT_CONF_SUBST([NO_GECOS_IN_PWENT])
#
# Define NO_SOCKADDR_STORAGE if your platform does not have struct
# sockaddr_storage.
AC_CHECK_TYPE(struct sockaddr_storage,
[NO_SOCKADDR_STORAGE=],
[NO_SOCKADDR_STORAGE=YesPlease],[
#include <sys/types.h>
#include <sys/socket.h>
])
GIT_CONF_SUBST([NO_SOCKADDR_STORAGE])
#
# Define NO_IPV6 if you lack IPv6 support and getaddrinfo().
AC_CHECK_TYPE([struct addrinfo],[
 GIT_CHECK_FUNC([getaddrinfo],
  [NO_IPV6=],
  [NO_IPV6=YesPlease])
],[NO_IPV6=YesPlease],[
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
])
GIT_CONF_SUBST([NO_IPV6])
#
# Define NO_REGEX if your C library lacks regex support with REG_STARTEND
# feature.
AC_CACHE_CHECK([whether the platform regex supports REG_STARTEND],
 [ac_cv_c_regex_with_reg_startend], [
AC_EGREP_CPP(yippeeyeswehaveit,
	AC_LANG_PROGRAM([AC_INCLUDES_DEFAULT
#include <regex.h>
],
[#ifdef REG_STARTEND
yippeeyeswehaveit
#endif
]),
	[ac_cv_c_regex_with_reg_startend=yes],
	[ac_cv_c_regex_with_reg_startend=no])
])
if test $ac_cv_c_regex_with_reg_startend = yes; then
	NO_REGEX=
else
	NO_REGEX=YesPlease
fi
GIT_CONF_SUBST([NO_REGEX])
#
# Define FREAD_READS_DIRECTORIES if your are on a system which succeeds
# when attempting to read from an fopen'ed directory.
AC_CACHE_CHECK([whether system succeeds to read fopen'ed directory],
 [ac_cv_fread_reads_directories],
[
AC_RUN_IFELSE(
	[AC_LANG_PROGRAM([AC_INCLUDES_DEFAULT],
		[[
		FILE *f = fopen(".", "r");
		return f != NULL;]])],
	[ac_cv_fread_reads_directories=no],
	[ac_cv_fread_reads_directories=yes])
])
if test $ac_cv_fread_reads_directories = yes; then
	FREAD_READS_DIRECTORIES=UnfortunatelyYes
else
	FREAD_READS_DIRECTORIES=
fi
GIT_CONF_SUBST([FREAD_READS_DIRECTORIES])
#
# Define SNPRINTF_RETURNS_BOGUS if your are on a system which snprintf()
# or vsnprintf() return -1 instead of number of characters which would
# have been written to the final string if enough space had been available.
AC_CACHE_CHECK([whether snprintf() and/or vsnprintf() return bogus value],
 [ac_cv_snprintf_returns_bogus],
[
AC_RUN_IFELSE(
	[AC_LANG_PROGRAM([AC_INCLUDES_DEFAULT
		#include "stdarg.h"

		int test_vsnprintf(char *str, size_t maxsize, const char *format, ...)
		{
		  int ret;
		  va_list ap;
		  va_start(ap, format);
		  ret = vsnprintf(str, maxsize, format, ap);
		  va_end(ap);
		  return ret;
		}],
		[[char buf[6];
		  if (test_vsnprintf(buf, 3, "%s", "12345") != 5
		      || strcmp(buf, "12")) return 1;
		  if (snprintf(buf, 3, "%s", "12345") != 5
		      || strcmp(buf, "12")) return 1]])],
	[ac_cv_snprintf_returns_bogus=no],
	[ac_cv_snprintf_returns_bogus=yes])
])
if test $ac_cv_snprintf_returns_bogus = yes; then
	SNPRINTF_RETURNS_BOGUS=UnfortunatelyYes
else
	SNPRINTF_RETURNS_BOGUS=
fi
GIT_CONF_SUBST([SNPRINTF_RETURNS_BOGUS])
#
# Define NEEDS_MODE_TRANSLATION if your OS strays from the typical file type
# bits in mode values.
AC_CACHE_CHECK([whether the platform uses typical file type bits],
 [ac_cv_sane_mode_bits], [
AC_EGREP_CPP(yippeeyeswehaveit,
	AC_LANG_PROGRAM([AC_INCLUDES_DEFAULT],
[#if S_IFMT == 0170000 && \
	S_IFREG == 0100000 && S_IFDIR == 0040000 && S_IFLNK == 0120000 && \
	S_IFBLK == 0060000 && S_IFCHR == 0020000 && \
	S_IFIFO == 0010000 && S_IFSOCK == 0140000
yippeeyeswehaveit
#endif
]),
	[ac_cv_sane_mode_bits=yes],
	[ac_cv_sane_mode_bits=no])
])
if test $ac_cv_sane_mode_bits = yes; then
	NEEDS_MODE_TRANSLATION=
else
	NEEDS_MODE_TRANSLATION=UnfortunatelyYes
fi
GIT_CONF_SUBST([NEEDS_MODE_TRANSLATION])


## Checks for library functions.
## (in default C library and libraries checked by AC_CHECK_LIB)
AC_MSG_NOTICE([CHECKS for library functions])
#
# Define NO_LIBGEN_H if you don't have libgen.h.
AC_CHECK_HEADER([libgen.h],
[NO_LIBGEN_H=],
[NO_LIBGEN_H=YesPlease])
GIT_CONF_SUBST([NO_LIBGEN_H])
#
# Define HAVE_PATHS_H if you have paths.h.
AC_CHECK_HEADER([paths.h],
[HAVE_PATHS_H=YesPlease],
[HAVE_PATHS_H=])
GIT_CONF_SUBST([HAVE_PATHS_H])
#
# Define HAVE_LIBCHARSET_H if have libcharset.h
AC_CHECK_HEADER([libcharset.h],
[HAVE_LIBCHARSET_H=YesPlease],
[HAVE_LIBCHARSET_H=])
GIT_CONF_SUBST([HAVE_LIBCHARSET_H])
#
# Define HAVE_STRINGS_H if you have strings.h
AC_CHECK_HEADER([strings.h],
[HAVE_STRINGS_H=YesPlease],
[HAVE_STRINGS_H=])
GIT_CONF_SUBST([HAVE_STRINGS_H])
# Define CHARSET_LIB if libiconv does not export the locale_charset symbol
# and libcharset does
CHARSET_LIB=
AC_CHECK_LIB([iconv], [locale_charset],
       [CHARSET_LIB=-liconv],
       [AC_CHECK_LIB([charset], [locale_charset],
                     [CHARSET_LIB=-lcharset])])
GIT_CONF_SUBST([CHARSET_LIB])
#
# Define HAVE_CLOCK_GETTIME=YesPlease if clock_gettime is available.
GIT_CHECK_FUNC(clock_gettime,
	[HAVE_CLOCK_GETTIME=YesPlease],
	[HAVE_CLOCK_GETTIME=])
GIT_CONF_SUBST([HAVE_CLOCK_GETTIME])

AC_DEFUN([CLOCK_MONOTONIC_SRC], [
AC_LANG_PROGRAM([[
#include <time.h>
clockid_t id = CLOCK_MONOTONIC;
]])])

#
# Define HAVE_CLOCK_MONOTONIC=YesPlease if CLOCK_MONOTONIC is available.
AC_MSG_CHECKING([for CLOCK_MONOTONIC])
AC_COMPILE_IFELSE([CLOCK_MONOTONIC_SRC],
	[AC_MSG_RESULT([yes])
	HAVE_CLOCK_MONOTONIC=YesPlease],
	[AC_MSG_RESULT([no])
	HAVE_CLOCK_MONOTONIC=])
GIT_CONF_SUBST([HAVE_CLOCK_MONOTONIC])

#
# Define HAVE_SYNC_FILE_RANGE=YesPlease if sync_file_range is available.
GIT_CHECK_FUNC(sync_file_range,
	[HAVE_SYNC_FILE_RANGE=YesPlease],
	[HAVE_SYNC_FILE_RANGE])
GIT_CONF_SUBST([HAVE_SYNC_FILE_RANGE])

#
# Define NO_SETITIMER if you don't have setitimer.
GIT_CHECK_FUNC(setitimer,
[NO_SETITIMER=],
[NO_SETITIMER=YesPlease])
GIT_CONF_SUBST([NO_SETITIMER])
#
# Define NO_STRCASESTR if you don't have strcasestr.
GIT_CHECK_FUNC(strcasestr,
[NO_STRCASESTR=],
[NO_STRCASESTR=YesPlease])
GIT_CONF_SUBST([NO_STRCASESTR])
#
# Define NO_MEMMEM if you don't have memmem.
GIT_CHECK_FUNC(memmem,
[NO_MEMMEM=],
[NO_MEMMEM=YesPlease])
GIT_CONF_SUBST([NO_MEMMEM])
#
# Define NO_STRLCPY if you don't have strlcpy.
GIT_CHECK_FUNC(strlcpy,
[NO_STRLCPY=],
[NO_STRLCPY=YesPlease])
GIT_CONF_SUBST([NO_STRLCPY])
#
# Define NO_UINTMAX_T if your platform does not have uintmax_t
AC_CHECK_TYPE(uintmax_t,
[NO_UINTMAX_T=],
[NO_UINTMAX_T=YesPlease],[
#include <inttypes.h>
])
GIT_CONF_SUBST([NO_UINTMAX_T])
#
# Define NO_STRTOUMAX if you don't have strtoumax in the C library.
GIT_CHECK_FUNC(strtoumax,
[NO_STRTOUMAX=],
[NO_STRTOUMAX=YesPlease])
GIT_CONF_SUBST([NO_STRTOUMAX])
#
# Define NO_SETENV if you don't have setenv in the C library.
GIT_CHECK_FUNC(setenv,
[NO_SETENV=],
[NO_SETENV=YesPlease])
GIT_CONF_SUBST([NO_SETENV])
#
# Define NO_UNSETENV if you don't have unsetenv in the C library.
GIT_CHECK_FUNC(unsetenv,
[NO_UNSETENV=],
[NO_UNSETENV=YesPlease])
GIT_CONF_SUBST([NO_UNSETENV])
#
# Define NO_MKDTEMP if you don't have mkdtemp in the C library.
GIT_CHECK_FUNC(mkdtemp,
[NO_MKDTEMP=],
[NO_MKDTEMP=YesPlease])
GIT_CONF_SUBST([NO_MKDTEMP])
#
# Define NO_INITGROUPS if you don't have initgroups in the C library.
GIT_CHECK_FUNC(initgroups,
[NO_INITGROUPS=],
[NO_INITGROUPS=YesPlease])
GIT_CONF_SUBST([NO_INITGROUPS])
#
# Define HAVE_GETDELIM if you have getdelim in the C library.
GIT_CHECK_FUNC(getdelim,
[HAVE_GETDELIM=YesPlease],
[HAVE_GETDELIM=])
GIT_CONF_SUBST([HAVE_GETDELIM])
#
#
# Define NO_MMAP if you want to avoid mmap.
#
# Define NO_ICONV if your libc does not properly support iconv.

AC_DEFUN([BSD_SYSCTL_SRC], [
AC_LANG_PROGRAM([[
#include <stddef.h>
#include <sys/types.h>
#include <sys/sysctl.h>
]],[[
int val, mib[2];
size_t len;
mib[0] = CTL_HW;
mib[1] = 1;
len = sizeof(val);
return sysctl(mib, 2, &val, &len, NULL, 0) ? 1 : 0;
]])])

#
# Define HAVE_BSD_SYSCTL=YesPlease if a BSD-compatible sysctl function is available.
AC_MSG_CHECKING([for BSD sysctl])
AC_COMPILE_IFELSE([BSD_SYSCTL_SRC],
	[AC_MSG_RESULT([yes])
	HAVE_BSD_SYSCTL=YesPlease],
	[AC_MSG_RESULT([no])
	HAVE_BSD_SYSCTL=])
GIT_CONF_SUBST([HAVE_BSD_SYSCTL])

## Other checks.
# Define USE_PIC if you need the main git objects to be built with -fPIC
# in order to build and link perl/Git.so.  x86-64 seems to need this.
#
# Define NO_SYMLINK_HEAD if you never want .git/HEAD to be a symbolic link.
# Enable it on Windows.  By default, symrefs are still used.
#
# Define NO_PTHREADS if we do not have pthreads.
#
# Define PTHREAD_LIBS to the linker flag used for Pthread support.
AC_DEFUN([PTHREADTEST_SRC], [
AC_LANG_PROGRAM([[
#include <pthread.h>
static void *noop(void *ignore) { return ignore; }
]], [[
	pthread_mutex_t test_mutex;
	pthread_key_t test_key;
	pthread_t th;
	int retcode = 0;
	void *ret = (void *)0;
	retcode |= pthread_key_create(&test_key, (void *)0);
	retcode |= pthread_mutex_init(&test_mutex,(void *)0);
	retcode |= pthread_mutex_lock(&test_mutex);
	retcode |= pthread_mutex_unlock(&test_mutex);
	retcode |= pthread_create(&th, ret, noop, ret);
	retcode |= pthread_join(th, &ret);
	return retcode;
]])])

dnl AC_LANG_CONFTEST([AC_LANG_PROGRAM(
dnl   [[#include <pthread.h>]],
dnl   [[pthread_mutex_t test_mutex;]]
dnl )])

NO_PTHREADS=UnfortunatelyYes
PTHREAD_LIBS=

if test -n "$USER_NOPTHREAD"; then
   AC_MSG_NOTICE([Skipping POSIX Threads at user request.])
# handle these separately since PTHREAD_CFLAGS could be '-lpthreads
# -D_REENTRANT' or some such.
elif test -z "$PTHREAD_CFLAGS"; then
  threads_found=no
  # Attempt to compile and link some code using pthreads to determine
  # required linker flags. The order is somewhat important here: We
  # first try it without any extra flags, to catch systems where
  # pthreads are part of the C library, then go on testing various other
  # flags. We do so to avoid false positives. For example, on Mac OS X
  # pthreads are part of the C library; moreover, the compiler allows us
  # to add "-mt" to the CFLAGS (although it will do nothing except
  # trigger a warning about an unused flag). Hence if we checked for
  # "-mt" before "" we would end up picking it. But unfortunately this
  # would then trigger compiler warnings on every single file we compile.
  for opt in "" -mt -pthread -lpthread; do
     old_CFLAGS="$CFLAGS"
     old_LIBS="$LIBS"
     case "$opt" in
        -l*)  LIBS="$opt $LIBS" ;;
        *)    CFLAGS="$opt $CFLAGS" ;;
     esac

     AC_MSG_CHECKING([for POSIX Threads with '$opt'])
     AC_LINK_IFELSE([PTHREADTEST_SRC],
	[AC_MSG_RESULT([yes])
		NO_PTHREADS=
		PTHREAD_LIBS="$opt"
		PTHREAD_CFLAGS="$opt"
		threads_found=yes
		break
	],
	[AC_MSG_RESULT([no])])
      CFLAGS="$old_CFLAGS"
      LIBS="$old_LIBS"
  done
  if test $threads_found != yes; then
    AC_CHECK_LIB([pthread], [pthread_create],
	[PTHREAD_LIBS="-lpthread"],
	[NO_PTHREADS=UnfortunatelyYes])
  fi
else
  old_CFLAGS="$CFLAGS"
  CFLAGS="$PTHREAD_CFLAGS $CFLAGS"
  AC_MSG_CHECKING([for POSIX Threads with '$PTHREAD_CFLAGS'])
  AC_LINK_IFELSE([PTHREADTEST_SRC],
	[AC_MSG_RESULT([yes])
		NO_PTHREADS=
		PTHREAD_LIBS="$PTHREAD_CFLAGS"
	],
	[AC_MSG_RESULT([no])])

  CFLAGS="$old_CFLAGS"
fi

CFLAGS="$old_CFLAGS"

GIT_CONF_SUBST([PTHREAD_CFLAGS])
GIT_CONF_SUBST([PTHREAD_LIBS])
GIT_CONF_SUBST([NO_PTHREADS])

## Output files
AC_CONFIG_FILES(["${config_file}":"${config_in}"])
AC_OUTPUT
