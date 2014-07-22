# The default target of this Makefile is...
all::

# Define V=1 to have a more verbose compile.
#
# Define SHELL_PATH to a POSIX shell if your /bin/sh is broken.
#
# Define SANE_TOOL_PATH to a colon-separated list of paths to prepend
# to PATH if your tools in /usr/bin are broken.
#
# Define SOCKLEN_T to a suitable type (such as 'size_t') if your
# system headers do not define a socklen_t type.
#
# Define INLINE to a suitable substitute (such as '__inline' or '') if git
# fails to compile with errors about undefined inline functions or similar.
#
# Define SNPRINTF_RETURNS_BOGUS if your are on a system which snprintf()
# or vsnprintf() return -1 instead of number of characters which would
# have been written to the final string if enough space had been available.
#
# Define FREAD_READS_DIRECTORIES if your are on a system which succeeds
# when attempting to read from an fopen'ed directory.
#
# Define NO_OPENSSL environment variable if you do not have OpenSSL.
# This also implies BLK_SHA1.
#
# Define USE_LIBPCRE if you have and want to use libpcre. git-grep will be
# able to use Perl-compatible regular expressions.
#
# Define LIBPCREDIR=/foo/bar if your libpcre header and library files are in
# /foo/bar/include and /foo/bar/lib directories.
#
# Define HAVE_ALLOCA_H if you have working alloca(3) defined in that header.
#
# Define NO_CURL if you do not have libcurl installed.  git-http-fetch and
# git-http-push are not built, and you cannot use http:// and https://
# transports (neither smart nor dumb).
#
# Define CURLDIR=/foo/bar if your curl header and library files are in
# /foo/bar/include and /foo/bar/lib directories.
#
# Define NO_EXPAT if you do not have expat installed.  git-http-push is
# not built, and you cannot push using http:// and https:// transports (dumb).
#
# Define EXPATDIR=/foo/bar if your expat header and library files are in
# /foo/bar/include and /foo/bar/lib directories.
#
# Define EXPAT_NEEDS_XMLPARSE_H if you have an old version of expat (e.g.,
# 1.1 or 1.2) that provides xmlparse.h instead of expat.h.
#
# Define NO_GETTEXT if you don't want Git output to be translated.
# A translated Git requires GNU libintl or another gettext implementation,
# plus libintl-perl at runtime.
#
# Define USE_GETTEXT_SCHEME and set it to 'fallthrough', if you don't trust
# the installed gettext translation of the shell scripts output.
#
# Define HAVE_LIBCHARSET_H if you haven't set NO_GETTEXT and you can't
# trust the langinfo.h's nl_langinfo(CODESET) function to return the
# current character set. GNU and Solaris have a nl_langinfo(CODESET),
# FreeBSD can use either, but MinGW and some others need to use
# libcharset.h's locale_charset() instead.
#
# Define CHARSET_LIB to the library you need to link with in order to
# use locale_charset() function.  On some platforms this needs to set to
# -lcharset, on others to -liconv .
#
# Define LIBC_CONTAINS_LIBINTL if your gettext implementation doesn't
# need -lintl when linking.
#
# Define NO_MSGFMT_EXTENDED_OPTIONS if your implementation of msgfmt
# doesn't support GNU extensions like --check and --statistics
#
# Define HAVE_PATHS_H if you have paths.h and want to use the default PATH
# it specifies.
#
# Define NO_D_INO_IN_DIRENT if you don't have d_ino in your struct dirent.
#
# Define NO_D_TYPE_IN_DIRENT if your platform defines DT_UNKNOWN but lacks
# d_type in struct dirent (Cygwin 1.5, fixed in Cygwin 1.7).
#
# Define HAVE_STRINGS_H if you have strings.h and need it for strcasecmp.
#
# Define NO_STRCASESTR if you don't have strcasestr.
#
# Define NO_MEMMEM if you don't have memmem.
#
# Define NO_GETPAGESIZE if you don't have getpagesize.
#
# Define NO_STRLCPY if you don't have strlcpy.
#
# Define NO_STRTOUMAX if you don't have both strtoimax and strtoumax in the
# C library. If your compiler also does not support long long or does not have
# strtoull, define NO_STRTOULL.
#
# Define NO_SETENV if you don't have setenv in the C library.
#
# Define NO_UNSETENV if you don't have unsetenv in the C library.
#
# Define NO_MKDTEMP if you don't have mkdtemp in the C library.
#
# Define MKDIR_WO_TRAILING_SLASH if your mkdir() can't deal with trailing slash.
#
# Define NO_MKSTEMPS if you don't have mkstemps in the C library.
#
# Define NO_GECOS_IN_PWENT if you don't have pw_gecos in struct passwd
# in the C library.
#
# Define NO_LIBGEN_H if you don't have libgen.h.
#
# Define NEEDS_LIBGEN if your libgen needs -lgen when linking
#
# Define NO_SYS_SELECT_H if you don't have sys/select.h.
#
# Define NO_SYMLINK_HEAD if you never want .git/HEAD to be a symbolic link.
# Enable it on Windows.  By default, symrefs are still used.
#
# Define NO_SVN_TESTS if you want to skip time-consuming SVN interoperability
# tests.  These tests take up a significant amount of the total test time
# but are not needed unless you plan to talk to SVN repos.
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
# Define NO_APPLE_COMMON_CRYPTO if you are building on Darwin/Mac OS X
# and do not want to use Apple's CommonCrypto library.  This allows you
# to provide your own OpenSSL library, for example from MacPorts.
#
# Define BLK_SHA1 environment variable to make use of the bundled
# optimized C SHA1 routine.
#
# Define PPC_SHA1 environment variable when running make to make use of
# a bundled SHA1 routine optimized for PowerPC.
#
# Define NEEDS_CRYPTO_WITH_SSL if you need -lcrypto when using -lssl (Darwin).
#
# Define NEEDS_SSL_WITH_CRYPTO if you need -lssl when using -lcrypto (Darwin).
#
# Define NEEDS_SSL_WITH_CURL if you need -lssl with -lcurl (Minix).
#
# Define NEEDS_IDN_WITH_CURL if you need -lidn when using -lcurl (Minix).
#
# Define NEEDS_LIBICONV if linking with libc is not enough (Darwin).
#
# Define NEEDS_LIBINTL_BEFORE_LIBICONV if you need libintl before libiconv.
#
# Define NO_INTPTR_T if you don't have intptr_t or uintptr_t.
#
# Define NO_UINTMAX_T if you don't have uintmax_t.
#
# Define NEEDS_SOCKET if linking with libc is not enough (SunOS,
# Patrick Mauritz).
#
# Define NEEDS_RESOLV if linking with -lnsl and/or -lsocket is not enough.
# Notably on Solaris hstrerror resides in libresolv and on Solaris 7
# inet_ntop and inet_pton additionally reside there.
#
# Define NO_MMAP if you want to avoid mmap.
#
# Define NO_SYS_POLL_H if you don't have sys/poll.h.
#
# Define NO_POLL if you do not have or don't want to use poll().
# This also implies NO_SYS_POLL_H.
#
# Define NEEDS_SYS_PARAM_H if you need to include sys/param.h to compile,
# *PLEASE* REPORT to git@vger.kernel.org if your platform needs this;
# we want to know more about the issue.
#
# Define NO_PTHREADS if you do not have or do not want to use Pthreads.
#
# Define NO_PREAD if you have a problem with pread() system call (e.g.
# cygwin1.dll before v1.5.22).
#
# Define NO_SETITIMER if you don't have setitimer()
#
# Define NO_STRUCT_ITIMERVAL if you don't have struct itimerval
# This also implies NO_SETITIMER
#
# Define NO_FAST_WORKING_DIRECTORY if accessing objects in pack files is
# generally faster on your platform than accessing the working directory.
#
# Define NO_TRUSTABLE_FILEMODE if your filesystem may claim to support
# the executable mode bit, but doesn't really do so.
#
# Define NO_IPV6 if you lack IPv6 support and getaddrinfo().
#
# Define NO_UNIX_SOCKETS if your system does not offer unix sockets.
#
# Define NO_SOCKADDR_STORAGE if your platform does not have struct
# sockaddr_storage.
#
# Define NO_ICONV if your libc does not properly support iconv.
#
# Define OLD_ICONV if your library has an old iconv(), where the second
# (input buffer pointer) parameter is declared with type (const char **).
#
# Define NO_DEFLATE_BOUND if your zlib does not have deflateBound.
#
# Define NO_R_TO_GCC_LINKER if your gcc does not like "-R/path/lib"
# that tells runtime paths to dynamic libraries;
# "-Wl,-rpath=/path/lib" is used instead.
#
# Define NO_NORETURN if using buggy versions of gcc 4.6+ and profile feedback,
# as the compiler can crash (http://gcc.gnu.org/bugzilla/show_bug.cgi?id=49299)
#
# Define USE_NSEC below if you want git to care about sub-second file mtimes
# and ctimes. Note that you need recent glibc (at least 2.2.4) for this, and
# it will BREAK YOUR LOCAL DIFFS! show-diff and anything using it will likely
# randomly break unless your underlying filesystem supports those sub-second
# times (my ext3 doesn't).
#
# Define USE_ST_TIMESPEC if your "struct stat" uses "st_ctimespec" instead of
# "st_ctim"
#
# Define NO_NSEC if your "struct stat" does not have "st_ctim.tv_nsec"
# available.  This automatically turns USE_NSEC off.
#
# Define USE_STDEV below if you want git to care about the underlying device
# change being considered an inode change from the update-index perspective.
#
# Define NO_ST_BLOCKS_IN_STRUCT_STAT if your platform does not have st_blocks
# field that counts the on-disk footprint in 512-byte blocks.
#
# Define DOCBOOK_XSL_172 if you want to format man pages with DocBook XSL v1.72
# (not v1.73 or v1.71).
#
# Define ASCIIDOC_ROFF if your DocBook XSL does not escape raw roff directives
# (versions 1.68.1 through v1.72).
#
# Define GNU_ROFF if your target system uses GNU groff.  This forces
# apostrophes to be ASCII so that cut&pasting examples to the shell
# will work.
#
# Define PERL_PATH to the path of your Perl binary (usually /usr/bin/perl).
#
# Define NO_PERL_MAKEMAKER if you cannot use Makefiles generated by perl's
# MakeMaker (e.g. using ActiveState under Cygwin).
#
# Define NO_PERL if you do not want Perl scripts or libraries at all.
#
# Define PYTHON_PATH to the path of your Python binary (often /usr/bin/python
# but /usr/bin/python2.7 on some platforms).
#
# Define NO_PYTHON if you do not want Python scripts or libraries at all.
#
# Define NO_TCLTK if you do not want Tcl/Tk GUI.
#
# The TCL_PATH variable governs the location of the Tcl interpreter
# used to optimize git-gui for your system.  Only used if NO_TCLTK
# is not set.  Defaults to the bare 'tclsh'.
#
# The TCLTK_PATH variable governs the location of the Tcl/Tk interpreter.
# If not set it defaults to the bare 'wish'. If it is set to the empty
# string then NO_TCLTK will be forced (this is used by configure script).
#
# Define INTERNAL_QSORT to use Git's implementation of qsort(), which
# is a simplified version of the merge sort used in glibc. This is
# recommended if Git triggers O(n^2) behavior in your platform's qsort().
#
# Define UNRELIABLE_FSTAT if your system's fstat does not return the same
# information on a not yet closed file that lstat would return for the same
# file after it was closed.
#
# Define OBJECT_CREATION_USES_RENAMES if your operating systems has problems
# when hardlinking a file to another name and unlinking the original file right
# away (some NTFS drivers seem to zero the contents in that scenario).
#
# Define NO_CROSS_DIRECTORY_HARDLINKS if you plan to distribute the installed
# programs as a tar, where bin/ and libexec/ might be on different file systems.
#
# Define NO_INSTALL_HARDLINKS if you prefer to use either symbolic links or
# copies to install built-in git commands e.g. git-cat-file.
#
# Define USE_NED_ALLOCATOR if you want to replace the platforms default
# memory allocators with the nedmalloc allocator written by Niall Douglas.
#
# Define NO_REGEX if you have no or inferior regex support in your C library.
#
# Define HAVE_DEV_TTY if your system can open /dev/tty to interact with the
# user.
#
# Define GETTEXT_POISON if you are debugging the choice of strings marked
# for translation.  In a GETTEXT_POISON build, you can turn all strings marked
# for translation into gibberish by setting the GIT_GETTEXT_POISON variable
# (to any value) in your environment.
#
# Define JSMIN to point to JavaScript minifier that functions as
# a filter to have gitweb.js minified.
#
# Define CSSMIN to point to a CSS minifier in order to generate a minified
# version of gitweb.css
#
# Define DEFAULT_PAGER to a sensible pager command (defaults to "less") if
# you want to use something different.  The value will be interpreted by the
# shell at runtime when it is used.
#
# Define DEFAULT_EDITOR to a sensible editor command (defaults to "vi") if you
# want to use something different.  The value will be interpreted by the shell
# if necessary when it is used.  Examples:
#
#   DEFAULT_EDITOR='~/bin/vi',
#   DEFAULT_EDITOR='$GIT_FALLBACK_EDITOR',
#   DEFAULT_EDITOR='"C:\Program Files\Vim\gvim.exe" --nofork'
#
# Define COMPUTE_HEADER_DEPENDENCIES to "yes" if you want dependencies on
# header files to be automatically computed, to avoid rebuilding objects when
# an unrelated header file changes.  Define it to "no" to use the hard-coded
# dependency rules.  The default is "auto", which means to use computed header
# dependencies if your compiler is detected to support it.
#
# Define CHECK_HEADER_DEPENDENCIES to check for problems in the hard-coded
# dependency rules.
#
# Define NATIVE_CRLF if your platform uses CRLF for line endings.
#
# Define XDL_FAST_HASH to use an alternative line-hashing method in
# the diff algorithm.  It gives a nice speedup if your processor has
# fast unaligned word loads.  Does NOT work on big-endian systems!
# Enabled by default on x86_64.
#
# Define GIT_USER_AGENT if you want to change how git identifies itself during
# network interactions.  The default is "git/$(GIT_VERSION)".
#
# Define DEFAULT_HELP_FORMAT to "man", "info" or "html"
# (defaults to "man") if you want to have a different default when
# "git help" is called without a parameter specifying the format.
#
# Define TEST_GIT_INDEX_VERSION to 2, 3 or 4 to run the test suite
# with a different indexfile format version.  If it isn't set the index
# file format used is index-v[23].
#
# Define GMTIME_UNRELIABLE_ERRORS if your gmtime() function does not
# return NULL when it receives a bogus time_t.
#
# Define HAVE_CLOCK_GETTIME if your platform has clock_gettime in librt.

GIT-VERSION-FILE: FORCE
	@$(SHELL_PATH) ./GIT-VERSION-GEN
-include GIT-VERSION-FILE

# CFLAGS and LDFLAGS are for the users to override from the command line.

CFLAGS = -g -O2 -Wall
LDFLAGS =
ALL_CFLAGS = $(CPPFLAGS) $(CFLAGS)
ALL_LDFLAGS = $(LDFLAGS)
STRIP ?= strip

# Among the variables below, these:
#   gitexecdir
#   template_dir
#   sysconfdir
# can be specified as a relative path some/where/else;
# this is interpreted as relative to $(prefix) and "git" at
# runtime figures out where they are based on the path to the executable.
# Additionally, the following will be treated as relative by "git" if they
# begin with "$(prefix)/":
#   mandir
#   infodir
#   htmldir
# This can help installing the suite in a relocatable way.

prefix = $(HOME)
bindir_relative = bin
bindir = $(prefix)/$(bindir_relative)
mandir = $(prefix)/share/man
infodir = $(prefix)/share/info
gitexecdir = libexec/git-core
mergetoolsdir = $(gitexecdir)/mergetools
sharedir = $(prefix)/share
gitwebdir = $(sharedir)/gitweb
localedir = $(sharedir)/locale
template_dir = share/git-core/templates
htmldir = $(prefix)/share/doc/git-doc
ETC_GITCONFIG = $(sysconfdir)/gitconfig
ETC_GITATTRIBUTES = $(sysconfdir)/gitattributes
lib = lib
# DESTDIR =
pathsep = :

mandir_relative = $(patsubst $(prefix)/%,%,$(mandir))
infodir_relative = $(patsubst $(prefix)/%,%,$(infodir))
htmldir_relative = $(patsubst $(prefix)/%,%,$(htmldir))

export prefix bindir sharedir sysconfdir gitwebdir localedir

CC = cc
AR = ar
RM = rm -f
DIFF = diff
TAR = tar
FIND = find
INSTALL = install
RPMBUILD = rpmbuild
TCL_PATH = tclsh
TCLTK_PATH = wish
XGETTEXT = xgettext
MSGFMT = msgfmt
PTHREAD_LIBS = -lpthread
PTHREAD_CFLAGS =
GCOV = gcov

export TCL_PATH TCLTK_PATH

SPARSE_FLAGS =



### --- END CONFIGURATION SECTION ---

# Those must not be GNU-specific; they are shared with perl/ which may
# be built by a different compiler. (Note that this is an artifact now
# but it still might be nice to keep that distinction.)
BASIC_CFLAGS = -I.
BASIC_LDFLAGS =

# Guard against environment variables
BUILTIN_OBJS =
BUILT_INS =
COMPAT_CFLAGS =
COMPAT_OBJS =
XDIFF_OBJS =
VCSSVN_OBJS =
GENERATED_H =
EXTRA_CPPFLAGS =
LIB_H =
LIB_OBJS =
PROGRAM_OBJS =
PROGRAMS =
SCRIPT_PERL =
SCRIPT_PYTHON =
SCRIPT_SH =
SCRIPT_LIB =
TEST_PROGRAMS_NEED_X =

# Having this variable in your environment would break pipelines because
# you cause "cd" to echo its destination to stdout.  It can also take
# scripts to unexpected places.  If you like CDPATH, define it for your
# interactive shell sessions without exporting it.
unexport CDPATH

SCRIPT_SH += git-am.sh
SCRIPT_SH += git-bisect.sh
SCRIPT_SH += git-difftool--helper.sh
SCRIPT_SH += git-filter-branch.sh
SCRIPT_SH += git-merge-octopus.sh
SCRIPT_SH += git-merge-one-file.sh
SCRIPT_SH += git-merge-resolve.sh
SCRIPT_SH += git-mergetool.sh
SCRIPT_SH += git-pull.sh
SCRIPT_SH += git-quiltimport.sh
SCRIPT_SH += git-rebase.sh
SCRIPT_SH += git-remote-testgit.sh
SCRIPT_SH += git-request-pull.sh
SCRIPT_SH += git-stash.sh
SCRIPT_SH += git-submodule.sh
SCRIPT_SH += git-web--browse.sh

SCRIPT_LIB += git-mergetool--lib
SCRIPT_LIB += git-parse-remote
SCRIPT_LIB += git-rebase--am
SCRIPT_LIB += git-rebase--interactive
SCRIPT_LIB += git-rebase--merge
SCRIPT_LIB += git-sh-setup
SCRIPT_LIB += git-sh-i18n

SCRIPT_PERL += git-add--interactive.perl
SCRIPT_PERL += git-difftool.perl
SCRIPT_PERL += git-archimport.perl
SCRIPT_PERL += git-cvsexportcommit.perl
SCRIPT_PERL += git-cvsimport.perl
SCRIPT_PERL += git-cvsserver.perl
SCRIPT_PERL += git-relink.perl
SCRIPT_PERL += git-send-email.perl
SCRIPT_PERL += git-svn.perl

SCRIPT_PYTHON += git-p4.py

NO_INSTALL += git-remote-testgit

# Generated files for scripts
SCRIPT_SH_GEN = $(patsubst %.sh,%,$(SCRIPT_SH))
SCRIPT_PERL_GEN = $(patsubst %.perl,%,$(SCRIPT_PERL))
SCRIPT_PYTHON_GEN = $(patsubst %.py,%,$(SCRIPT_PYTHON))

SCRIPT_SH_INS = $(filter-out $(NO_INSTALL),$(SCRIPT_SH_GEN))
SCRIPT_PERL_INS = $(filter-out $(NO_INSTALL),$(SCRIPT_PERL_GEN))
SCRIPT_PYTHON_INS = $(filter-out $(NO_INSTALL),$(SCRIPT_PYTHON_GEN))

# Individual rules to allow e.g.
# "make -C ../.. SCRIPT_PERL=contrib/foo/bar.perl build-perl-script"
# from subdirectories like contrib/*/
.PHONY: build-perl-script build-sh-script build-python-script
build-perl-script: $(SCRIPT_PERL_GEN)
build-sh-script: $(SCRIPT_SH_GEN)
build-python-script: $(SCRIPT_PYTHON_GEN)

.PHONY: install-perl-script install-sh-script install-python-script
install-sh-script: $(SCRIPT_SH_INS)
	$(INSTALL) $^ '$(DESTDIR_SQ)$(gitexec_instdir_SQ)'
install-perl-script: $(SCRIPT_PERL_INS)
	$(INSTALL) $^ '$(DESTDIR_SQ)$(gitexec_instdir_SQ)'
install-python-script: $(SCRIPT_PYTHON_INS)
	$(INSTALL) $^ '$(DESTDIR_SQ)$(gitexec_instdir_SQ)'

.PHONY: clean-perl-script clean-sh-script clean-python-script
clean-sh-script:
	$(RM) $(SCRIPT_SH_GEN)
clean-perl-script:
	$(RM) $(SCRIPT_PERL_GEN)
clean-python-script:
	$(RM) $(SCRIPT_PYTHON_GEN)

SCRIPTS = $(SCRIPT_SH_INS) \
	  $(SCRIPT_PERL_INS) \
	  $(SCRIPT_PYTHON_INS) \
	  git-instaweb

ETAGS_TARGET = TAGS

# Empty...
EXTRA_PROGRAMS =

# ... and all the rest that could be moved out of bindir to gitexecdir
PROGRAMS += $(EXTRA_PROGRAMS)

PROGRAM_OBJS += credential-store.o
PROGRAM_OBJS += daemon.o
PROGRAM_OBJS += fast-import.o
PROGRAM_OBJS += http-backend.o
PROGRAM_OBJS += imap-send.o
PROGRAM_OBJS += sh-i18n--envsubst.o
PROGRAM_OBJS += shell.o
PROGRAM_OBJS += show-index.o
PROGRAM_OBJS += upload-pack.o
PROGRAM_OBJS += remote-testsvn.o

# Binary suffix, set to .exe for Windows builds
X =

PROGRAMS += $(patsubst %.o,git-%$X,$(PROGRAM_OBJS))

TEST_PROGRAMS_NEED_X += test-chmtime
TEST_PROGRAMS_NEED_X += test-ctype
TEST_PROGRAMS_NEED_X += test-date
TEST_PROGRAMS_NEED_X += test-delta
TEST_PROGRAMS_NEED_X += test-dump-cache-tree
TEST_PROGRAMS_NEED_X += test-dump-split-index
TEST_PROGRAMS_NEED_X += test-genrandom
TEST_PROGRAMS_NEED_X += test-hashmap
TEST_PROGRAMS_NEED_X += test-index-version
TEST_PROGRAMS_NEED_X += test-line-buffer
TEST_PROGRAMS_NEED_X += test-match-trees
TEST_PROGRAMS_NEED_X += test-mergesort
TEST_PROGRAMS_NEED_X += test-mktemp
TEST_PROGRAMS_NEED_X += test-parse-options
TEST_PROGRAMS_NEED_X += test-path-utils
TEST_PROGRAMS_NEED_X += test-prio-queue
TEST_PROGRAMS_NEED_X += test-read-cache
TEST_PROGRAMS_NEED_X += test-regex
TEST_PROGRAMS_NEED_X += test-revision-walking
TEST_PROGRAMS_NEED_X += test-run-command
TEST_PROGRAMS_NEED_X += test-scrap-cache-tree
TEST_PROGRAMS_NEED_X += test-sha1
TEST_PROGRAMS_NEED_X += test-sigchain
TEST_PROGRAMS_NEED_X += test-string-list
TEST_PROGRAMS_NEED_X += test-subprocess
TEST_PROGRAMS_NEED_X += test-svn-fe
TEST_PROGRAMS_NEED_X += test-urlmatch-normalization
TEST_PROGRAMS_NEED_X += test-wildmatch

TEST_PROGRAMS = $(patsubst %,%$X,$(TEST_PROGRAMS_NEED_X))

# List built-in command $C whose implementation cmd_$C() is not in
# builtin/$C.o but is linked in as part of some other command.
BUILT_INS += $(patsubst builtin/%.o,git-%$X,$(BUILTIN_OBJS))

BUILT_INS += git-cherry$X
BUILT_INS += git-cherry-pick$X
BUILT_INS += git-format-patch$X
BUILT_INS += git-fsck-objects$X
BUILT_INS += git-init$X
BUILT_INS += git-merge-subtree$X
BUILT_INS += git-show$X
BUILT_INS += git-stage$X
BUILT_INS += git-status$X
BUILT_INS += git-whatchanged$X

# what 'all' will build and 'install' will install in gitexecdir,
# excluding programs for built-in commands
ALL_PROGRAMS = $(PROGRAMS) $(SCRIPTS)

# what 'all' will build but not install in gitexecdir
OTHER_PROGRAMS = git$X

# what test wrappers are needed and 'install' will install, in bindir
BINDIR_PROGRAMS_NEED_X += git
BINDIR_PROGRAMS_NEED_X += git-upload-pack
BINDIR_PROGRAMS_NEED_X += git-receive-pack
BINDIR_PROGRAMS_NEED_X += git-upload-archive
BINDIR_PROGRAMS_NEED_X += git-shell

BINDIR_PROGRAMS_NO_X += git-cvsserver

# Set paths to tools early so that they can be used for version tests.
ifndef SHELL_PATH
	SHELL_PATH = /bin/sh
endif
ifndef PERL_PATH
	PERL_PATH = /usr/bin/perl
endif
ifndef PYTHON_PATH
	PYTHON_PATH = /usr/bin/python
endif

export PERL_PATH
export PYTHON_PATH

LIB_FILE = libgit.a
XDIFF_LIB = xdiff/lib.a
VCSSVN_LIB = vcs-svn/lib.a

GENERATED_H += common-cmds.h

LIB_H += advice.h
LIB_H += archive.h
LIB_H += argv-array.h
LIB_H += attr.h
LIB_H += bisect.h
LIB_H += blob.h
LIB_H += branch.h
LIB_H += builtin.h
LIB_H += bulk-checkin.h
LIB_H += bundle.h
LIB_H += cache-tree.h
LIB_H += cache.h
LIB_H += color.h
LIB_H += column.h
LIB_H += commit.h
LIB_H += compat/bswap.h
LIB_H += compat/mingw.h
LIB_H += compat/obstack.h
LIB_H += compat/poll/poll.h
LIB_H += compat/precompose_utf8.h
LIB_H += compat/terminal.h
LIB_H += compat/win32/dirent.h
LIB_H += compat/win32/pthread.h
LIB_H += compat/win32/syslog.h
LIB_H += connected.h
LIB_H += convert.h
LIB_H += credential.h
LIB_H += csum-file.h
LIB_H += decorate.h
LIB_H += delta.h
LIB_H += diff.h
LIB_H += diffcore.h
LIB_H += dir.h
LIB_H += exec_cmd.h
LIB_H += ewah/ewok.h
LIB_H += ewah/ewok_rlw.h
LIB_H += fetch-pack.h
LIB_H += fmt-merge-msg.h
LIB_H += fsck.h
LIB_H += gettext.h
LIB_H += git-compat-util.h
LIB_H += gpg-interface.h
LIB_H += graph.h
LIB_H += grep.h
LIB_H += hashmap.h
LIB_H += help.h
LIB_H += http.h
LIB_H += kwset.h
LIB_H += levenshtein.h
LIB_H += line-log.h
LIB_H += line-range.h
LIB_H += list-objects.h
LIB_H += ll-merge.h
LIB_H += log-tree.h
LIB_H += mailmap.h
LIB_H += merge-blobs.h
LIB_H += merge-recursive.h
LIB_H += mergesort.h
LIB_H += notes-cache.h
LIB_H += notes-merge.h
LIB_H += notes-utils.h
LIB_H += notes.h
LIB_H += object.h
LIB_H += pack-objects.h
LIB_H += pack-revindex.h
LIB_H += pack.h
LIB_H += pack-bitmap.h
LIB_H += parse-options.h
LIB_H += patch-ids.h
LIB_H += pathspec.h
LIB_H += pkt-line.h
LIB_H += prio-queue.h
LIB_H += progress.h
LIB_H += prompt.h
LIB_H += quote.h
LIB_H += reachable.h
LIB_H += reflog-walk.h
LIB_H += refs.h
LIB_H += remote.h
LIB_H += rerere.h
LIB_H += resolve-undo.h
LIB_H += revision.h
LIB_H += run-command.h
LIB_H += send-pack.h
LIB_H += sequencer.h
LIB_H += sha1-array.h
LIB_H += sha1-lookup.h
LIB_H += shortlog.h
LIB_H += sideband.h
LIB_H += sigchain.h
LIB_H += strbuf.h
LIB_H += streaming.h
LIB_H += string-list.h
LIB_H += submodule.h
LIB_H += tag.h
LIB_H += tar.h
LIB_H += thread-utils.h
LIB_H += transport.h
LIB_H += tree-walk.h
LIB_H += tree.h
LIB_H += unpack-trees.h
LIB_H += unicode_width.h
LIB_H += url.h
LIB_H += urlmatch.h
LIB_H += userdiff.h
LIB_H += utf8.h
LIB_H += varint.h
LIB_H += vcs-svn/fast_export.h
LIB_H += vcs-svn/line_buffer.h
LIB_H += vcs-svn/repo_tree.h
LIB_H += vcs-svn/sliding_window.h
LIB_H += vcs-svn/svndiff.h
LIB_H += vcs-svn/svndump.h
LIB_H += walker.h
LIB_H += wildmatch.h
LIB_H += wt-status.h
LIB_H += xdiff-interface.h
LIB_H += xdiff/xdiff.h
LIB_H += xdiff/xdiffi.h
LIB_H += xdiff/xemit.h
LIB_H += xdiff/xinclude.h
LIB_H += xdiff/xmacros.h
LIB_H += xdiff/xprepare.h
LIB_H += xdiff/xtypes.h
LIB_H += xdiff/xutils.h

LIB_OBJS += abspath.o
LIB_OBJS += advice.o
LIB_OBJS += alias.o
LIB_OBJS += alloc.o
LIB_OBJS += archive.o
LIB_OBJS += archive-tar.o
LIB_OBJS += archive-zip.o
LIB_OBJS += argv-array.o
LIB_OBJS += attr.o
LIB_OBJS += base85.o
LIB_OBJS += bisect.o
LIB_OBJS += blob.o
LIB_OBJS += branch.o
LIB_OBJS += bulk-checkin.o
LIB_OBJS += bundle.o
LIB_OBJS += cache-tree.o
LIB_OBJS += color.o
LIB_OBJS += column.o
LIB_OBJS += combine-diff.o
LIB_OBJS += commit.o
LIB_OBJS += compat/obstack.o
LIB_OBJS += compat/terminal.o
LIB_OBJS += config.o
LIB_OBJS += connect.o
LIB_OBJS += connected.o
LIB_OBJS += convert.o
LIB_OBJS += copy.o
LIB_OBJS += credential.o
LIB_OBJS += csum-file.o
LIB_OBJS += ctype.o
LIB_OBJS += date.o
LIB_OBJS += decorate.o
LIB_OBJS += diffcore-break.o
LIB_OBJS += diffcore-delta.o
LIB_OBJS += diffcore-order.o
LIB_OBJS += diffcore-pickaxe.o
LIB_OBJS += diffcore-rename.o
LIB_OBJS += diff-delta.o
LIB_OBJS += diff-lib.o
LIB_OBJS += diff-no-index.o
LIB_OBJS += diff.o
LIB_OBJS += dir.o
LIB_OBJS += editor.o
LIB_OBJS += entry.o
LIB_OBJS += environment.o
LIB_OBJS += ewah/bitmap.o
LIB_OBJS += ewah/ewah_bitmap.o
LIB_OBJS += ewah/ewah_io.o
LIB_OBJS += ewah/ewah_rlw.o
LIB_OBJS += exec_cmd.o
LIB_OBJS += fetch-pack.o
LIB_OBJS += fsck.o
LIB_OBJS += gettext.o
LIB_OBJS += gpg-interface.o
LIB_OBJS += graph.o
LIB_OBJS += grep.o
LIB_OBJS += hashmap.o
LIB_OBJS += help.o
LIB_OBJS += hex.o
LIB_OBJS += ident.o
LIB_OBJS += kwset.o
LIB_OBJS += levenshtein.o
LIB_OBJS += line-log.o
LIB_OBJS += line-range.o
LIB_OBJS += list-objects.o
LIB_OBJS += ll-merge.o
LIB_OBJS += lockfile.o
LIB_OBJS += log-tree.o
LIB_OBJS += mailmap.o
LIB_OBJS += match-trees.o
LIB_OBJS += merge.o
LIB_OBJS += merge-blobs.o
LIB_OBJS += merge-recursive.o
LIB_OBJS += mergesort.o
LIB_OBJS += name-hash.o
LIB_OBJS += notes.o
LIB_OBJS += notes-cache.o
LIB_OBJS += notes-merge.o
LIB_OBJS += notes-utils.o
LIB_OBJS += object.o
LIB_OBJS += pack-bitmap.o
LIB_OBJS += pack-bitmap-write.o
LIB_OBJS += pack-check.o
LIB_OBJS += pack-objects.o
LIB_OBJS += pack-revindex.o
LIB_OBJS += pack-write.o
LIB_OBJS += pager.o
LIB_OBJS += parse-options.o
LIB_OBJS += parse-options-cb.o
LIB_OBJS += patch-delta.o
LIB_OBJS += patch-ids.o
LIB_OBJS += path.o
LIB_OBJS += pathspec.o
LIB_OBJS += pkt-line.o
LIB_OBJS += preload-index.o
LIB_OBJS += pretty.o
LIB_OBJS += prio-queue.o
LIB_OBJS += progress.o
LIB_OBJS += prompt.o
LIB_OBJS += quote.o
LIB_OBJS += reachable.o
LIB_OBJS += read-cache.o
LIB_OBJS += reflog-walk.o
LIB_OBJS += refs.o
LIB_OBJS += remote.o
LIB_OBJS += replace_object.o
LIB_OBJS += rerere.o
LIB_OBJS += resolve-undo.o
LIB_OBJS += revision.o
LIB_OBJS += run-command.o
LIB_OBJS += send-pack.o
LIB_OBJS += sequencer.o
LIB_OBJS += server-info.o
LIB_OBJS += setup.o
LIB_OBJS += sha1-array.o
LIB_OBJS += sha1-lookup.o
LIB_OBJS += sha1_file.o
LIB_OBJS += sha1_name.o
LIB_OBJS += shallow.o
LIB_OBJS += sideband.o
LIB_OBJS += sigchain.o
LIB_OBJS += split-index.o
LIB_OBJS += strbuf.o
LIB_OBJS += streaming.o
LIB_OBJS += string-list.o
LIB_OBJS += submodule.o
LIB_OBJS += symlinks.o
LIB_OBJS += tag.o
LIB_OBJS += trace.o
LIB_OBJS += transport.o
LIB_OBJS += transport-helper.o
LIB_OBJS += tree-diff.o
LIB_OBJS += tree.o
LIB_OBJS += tree-walk.o
LIB_OBJS += unpack-trees.o
LIB_OBJS += url.o
LIB_OBJS += urlmatch.o
LIB_OBJS += usage.o
LIB_OBJS += userdiff.o
LIB_OBJS += utf8.o
LIB_OBJS += varint.o
LIB_OBJS += version.o
LIB_OBJS += versioncmp.o
LIB_OBJS += walker.o
LIB_OBJS += wildmatch.o
LIB_OBJS += wrapper.o
LIB_OBJS += write_or_die.o
LIB_OBJS += ws.o
LIB_OBJS += wt-status.o
LIB_OBJS += xdiff-interface.o
LIB_OBJS += zlib.o

BUILTIN_OBJS += builtin/add.o
BUILTIN_OBJS += builtin/annotate.o
BUILTIN_OBJS += builtin/apply.o
BUILTIN_OBJS += builtin/archive.o
BUILTIN_OBJS += builtin/bisect--helper.o
BUILTIN_OBJS += builtin/blame.o
BUILTIN_OBJS += builtin/branch.o
BUILTIN_OBJS += builtin/bundle.o
BUILTIN_OBJS += builtin/cat-file.o
BUILTIN_OBJS += builtin/check-attr.o
BUILTIN_OBJS += builtin/check-ignore.o
BUILTIN_OBJS += builtin/check-mailmap.o
BUILTIN_OBJS += builtin/check-ref-format.o
BUILTIN_OBJS += builtin/checkout-index.o
BUILTIN_OBJS += builtin/checkout.o
BUILTIN_OBJS += builtin/clean.o
BUILTIN_OBJS += builtin/clone.o
BUILTIN_OBJS += builtin/column.o
BUILTIN_OBJS += builtin/commit-tree.o
BUILTIN_OBJS += builtin/commit.o
BUILTIN_OBJS += builtin/config.o
BUILTIN_OBJS += builtin/count-objects.o
BUILTIN_OBJS += builtin/credential.o
BUILTIN_OBJS += builtin/describe.o
BUILTIN_OBJS += builtin/diff-files.o
BUILTIN_OBJS += builtin/diff-index.o
BUILTIN_OBJS += builtin/diff-tree.o
BUILTIN_OBJS += builtin/diff.o
BUILTIN_OBJS += builtin/fast-export.o
BUILTIN_OBJS += builtin/fetch-pack.o
BUILTIN_OBJS += builtin/fetch.o
BUILTIN_OBJS += builtin/fmt-merge-msg.o
BUILTIN_OBJS += builtin/for-each-ref.o
BUILTIN_OBJS += builtin/fsck.o
BUILTIN_OBJS += builtin/gc.o
BUILTIN_OBJS += builtin/get-tar-commit-id.o
BUILTIN_OBJS += builtin/grep.o
BUILTIN_OBJS += builtin/hash-object.o
BUILTIN_OBJS += builtin/help.o
BUILTIN_OBJS += builtin/index-pack.o
BUILTIN_OBJS += builtin/init-db.o
BUILTIN_OBJS += builtin/log.o
BUILTIN_OBJS += builtin/ls-files.o
BUILTIN_OBJS += builtin/ls-remote.o
BUILTIN_OBJS += builtin/ls-tree.o
BUILTIN_OBJS += builtin/mailinfo.o
BUILTIN_OBJS += builtin/mailsplit.o
BUILTIN_OBJS += builtin/merge.o
BUILTIN_OBJS += builtin/merge-base.o
BUILTIN_OBJS += builtin/merge-file.o
BUILTIN_OBJS += builtin/merge-index.o
BUILTIN_OBJS += builtin/merge-ours.o
BUILTIN_OBJS += builtin/merge-recursive.o
BUILTIN_OBJS += builtin/merge-tree.o
BUILTIN_OBJS += builtin/mktag.o
BUILTIN_OBJS += builtin/mktree.o
BUILTIN_OBJS += builtin/mv.o
BUILTIN_OBJS += builtin/name-rev.o
BUILTIN_OBJS += builtin/notes.o
BUILTIN_OBJS += builtin/pack-objects.o
BUILTIN_OBJS += builtin/pack-redundant.o
BUILTIN_OBJS += builtin/pack-refs.o
BUILTIN_OBJS += builtin/patch-id.o
BUILTIN_OBJS += builtin/prune-packed.o
BUILTIN_OBJS += builtin/prune.o
BUILTIN_OBJS += builtin/push.o
BUILTIN_OBJS += builtin/read-tree.o
BUILTIN_OBJS += builtin/receive-pack.o
BUILTIN_OBJS += builtin/reflog.o
BUILTIN_OBJS += builtin/remote.o
BUILTIN_OBJS += builtin/remote-ext.o
BUILTIN_OBJS += builtin/remote-fd.o
BUILTIN_OBJS += builtin/repack.o
BUILTIN_OBJS += builtin/replace.o
BUILTIN_OBJS += builtin/rerere.o
BUILTIN_OBJS += builtin/reset.o
BUILTIN_OBJS += builtin/rev-list.o
BUILTIN_OBJS += builtin/rev-parse.o
BUILTIN_OBJS += builtin/revert.o
BUILTIN_OBJS += builtin/rm.o
BUILTIN_OBJS += builtin/send-pack.o
BUILTIN_OBJS += builtin/shortlog.o
BUILTIN_OBJS += builtin/show-branch.o
BUILTIN_OBJS += builtin/show-ref.o
BUILTIN_OBJS += builtin/stripspace.o
BUILTIN_OBJS += builtin/symbolic-ref.o
BUILTIN_OBJS += builtin/tag.o
BUILTIN_OBJS += builtin/unpack-file.o
BUILTIN_OBJS += builtin/unpack-objects.o
BUILTIN_OBJS += builtin/update-index.o
BUILTIN_OBJS += builtin/update-ref.o
BUILTIN_OBJS += builtin/update-server-info.o
BUILTIN_OBJS += builtin/upload-archive.o
BUILTIN_OBJS += builtin/var.o
BUILTIN_OBJS += builtin/verify-commit.o
BUILTIN_OBJS += builtin/verify-pack.o
BUILTIN_OBJS += builtin/verify-tag.o
BUILTIN_OBJS += builtin/write-tree.o

GITLIBS = $(LIB_FILE) $(XDIFF_LIB)
EXTLIBS =

GIT_USER_AGENT = git/$(GIT_VERSION)

include config.mak.uname
-include config.mak.autogen
-include config.mak

ifndef sysconfdir
ifeq ($(prefix),/usr)
sysconfdir = /etc
else
sysconfdir = etc
endif
endif

ifdef CHECK_HEADER_DEPENDENCIES
COMPUTE_HEADER_DEPENDENCIES = no
USE_COMPUTED_HEADER_DEPENDENCIES =
endif

ifndef COMPUTE_HEADER_DEPENDENCIES
COMPUTE_HEADER_DEPENDENCIES = auto
endif

ifeq ($(COMPUTE_HEADER_DEPENDENCIES),auto)
dep_check = $(shell $(CC) $(ALL_CFLAGS) \
	-c -MF /dev/null -MQ /dev/null -MMD -MP \
	-x c /dev/null -o /dev/null 2>&1; \
	echo $$?)
ifeq ($(dep_check),0)
override COMPUTE_HEADER_DEPENDENCIES = yes
else
override COMPUTE_HEADER_DEPENDENCIES = no
endif
endif

ifeq ($(COMPUTE_HEADER_DEPENDENCIES),yes)
USE_COMPUTED_HEADER_DEPENDENCIES = YesPlease
else
ifneq ($(COMPUTE_HEADER_DEPENDENCIES),no)
$(error please set COMPUTE_HEADER_DEPENDENCIES to yes, no, or auto \
(not "$(COMPUTE_HEADER_DEPENDENCIES)"))
endif
endif

ifdef SANE_TOOL_PATH
SANE_TOOL_PATH_SQ = $(subst ','\'',$(SANE_TOOL_PATH))
BROKEN_PATH_FIX = 's|^\# @@BROKEN_PATH_FIX@@$$|git_broken_path_fix $(SANE_TOOL_PATH_SQ)|'
PATH := $(SANE_TOOL_PATH):${PATH}
else
BROKEN_PATH_FIX = '/^\# @@BROKEN_PATH_FIX@@$$/d'
endif

ifneq (,$(INLINE))
	BASIC_CFLAGS += -Dinline=$(INLINE)
endif

ifneq (,$(SOCKLEN_T))
	BASIC_CFLAGS += -Dsocklen_t=$(SOCKLEN_T)
endif

ifeq ($(uname_S),Darwin)
	ifndef NO_FINK
		ifeq ($(shell test -d /sw/lib && echo y),y)
			BASIC_CFLAGS += -I/sw/include
			BASIC_LDFLAGS += -L/sw/lib
		endif
	endif
	ifndef NO_DARWIN_PORTS
		ifeq ($(shell test -d /opt/local/lib && echo y),y)
			BASIC_CFLAGS += -I/opt/local/include
			BASIC_LDFLAGS += -L/opt/local/lib
		endif
	endif
	ifndef NO_APPLE_COMMON_CRYPTO
		APPLE_COMMON_CRYPTO = YesPlease
		COMPAT_CFLAGS += -DAPPLE_COMMON_CRYPTO
	endif
	NO_REGEX = YesPlease
	PTHREAD_LIBS =
endif

ifndef CC_LD_DYNPATH
	ifdef NO_R_TO_GCC_LINKER
		# Some gcc does not accept and pass -R to the linker to specify
		# the runtime dynamic library path.
		CC_LD_DYNPATH = -Wl,-rpath,
	else
		CC_LD_DYNPATH = -R
	endif
endif

ifdef NO_LIBGEN_H
	COMPAT_CFLAGS += -DNO_LIBGEN_H
	COMPAT_OBJS += compat/basename.o
endif

ifdef USE_LIBPCRE
	BASIC_CFLAGS += -DUSE_LIBPCRE
	ifdef LIBPCREDIR
		BASIC_CFLAGS += -I$(LIBPCREDIR)/include
		EXTLIBS += -L$(LIBPCREDIR)/$(lib) $(CC_LD_DYNPATH)$(LIBPCREDIR)/$(lib)
	endif
	EXTLIBS += -lpcre
endif

ifdef HAVE_ALLOCA_H
	BASIC_CFLAGS += -DHAVE_ALLOCA_H
endif

ifdef NO_CURL
	BASIC_CFLAGS += -DNO_CURL
	REMOTE_CURL_PRIMARY =
	REMOTE_CURL_ALIASES =
	REMOTE_CURL_NAMES =
else
	ifdef CURLDIR
		# Try "-Wl,-rpath=$(CURLDIR)/$(lib)" in such a case.
		BASIC_CFLAGS += -I$(CURLDIR)/include
		CURL_LIBCURL = -L$(CURLDIR)/$(lib) $(CC_LD_DYNPATH)$(CURLDIR)/$(lib) -lcurl
	else
		CURL_LIBCURL = -lcurl
	endif
	ifdef NEEDS_SSL_WITH_CURL
		CURL_LIBCURL += -lssl
		ifdef NEEDS_CRYPTO_WITH_SSL
			CURL_LIBCURL += -lcrypto
		endif
	endif
	ifdef NEEDS_IDN_WITH_CURL
		CURL_LIBCURL += -lidn
	endif

	REMOTE_CURL_PRIMARY = git-remote-http$X
	REMOTE_CURL_ALIASES = git-remote-https$X git-remote-ftp$X git-remote-ftps$X
	REMOTE_CURL_NAMES = $(REMOTE_CURL_PRIMARY) $(REMOTE_CURL_ALIASES)
	PROGRAM_OBJS += http-fetch.o
	PROGRAMS += $(REMOTE_CURL_NAMES)
	curl_check := $(shell (echo 070908; curl-config --vernum) 2>/dev/null | sort -r | sed -ne 2p)
	ifeq "$(curl_check)" "070908"
		ifndef NO_EXPAT
			PROGRAM_OBJS += http-push.o
		endif
	endif
	ifndef NO_EXPAT
		ifdef EXPATDIR
			BASIC_CFLAGS += -I$(EXPATDIR)/include
			EXPAT_LIBEXPAT = -L$(EXPATDIR)/$(lib) $(CC_LD_DYNPATH)$(EXPATDIR)/$(lib) -lexpat
		else
			EXPAT_LIBEXPAT = -lexpat
		endif
		ifdef EXPAT_NEEDS_XMLPARSE_H
			BASIC_CFLAGS += -DEXPAT_NEEDS_XMLPARSE_H
		endif
	endif
endif

ifdef ZLIB_PATH
	BASIC_CFLAGS += -I$(ZLIB_PATH)/include
	EXTLIBS += -L$(ZLIB_PATH)/$(lib) $(CC_LD_DYNPATH)$(ZLIB_PATH)/$(lib)
endif
EXTLIBS += -lz

ifndef NO_OPENSSL
	OPENSSL_LIBSSL = -lssl
	ifdef OPENSSLDIR
		BASIC_CFLAGS += -I$(OPENSSLDIR)/include
		OPENSSL_LINK = -L$(OPENSSLDIR)/$(lib) $(CC_LD_DYNPATH)$(OPENSSLDIR)/$(lib)
	else
		OPENSSL_LINK =
	endif
	ifdef NEEDS_CRYPTO_WITH_SSL
		OPENSSL_LIBSSL += -lcrypto
	endif
else
	BASIC_CFLAGS += -DNO_OPENSSL
	BLK_SHA1 = 1
	OPENSSL_LIBSSL =
endif
ifdef NO_OPENSSL
	LIB_4_CRYPTO =
else
ifdef NEEDS_SSL_WITH_CRYPTO
	LIB_4_CRYPTO = $(OPENSSL_LINK) -lcrypto -lssl
else
	LIB_4_CRYPTO = $(OPENSSL_LINK) -lcrypto
endif
ifdef APPLE_COMMON_CRYPTO
	LIB_4_CRYPTO += -framework Security -framework CoreFoundation
endif
endif
ifdef NEEDS_LIBICONV
	ifdef ICONVDIR
		BASIC_CFLAGS += -I$(ICONVDIR)/include
		ICONV_LINK = -L$(ICONVDIR)/$(lib) $(CC_LD_DYNPATH)$(ICONVDIR)/$(lib)
	else
		ICONV_LINK =
	endif
	ifdef NEEDS_LIBINTL_BEFORE_LIBICONV
		ICONV_LINK += -lintl
	endif
	EXTLIBS += $(ICONV_LINK) -liconv
endif
ifdef NEEDS_LIBGEN
	EXTLIBS += -lgen
endif
ifndef NO_GETTEXT
ifndef LIBC_CONTAINS_LIBINTL
	EXTLIBS += -lintl
endif
endif
ifdef NEEDS_SOCKET
	EXTLIBS += -lsocket
endif
ifdef NEEDS_NSL
	EXTLIBS += -lnsl
endif
ifdef NEEDS_RESOLV
	EXTLIBS += -lresolv
endif
ifdef NO_D_TYPE_IN_DIRENT
	BASIC_CFLAGS += -DNO_D_TYPE_IN_DIRENT
endif
ifdef NO_D_INO_IN_DIRENT
	BASIC_CFLAGS += -DNO_D_INO_IN_DIRENT
endif
ifdef NO_GECOS_IN_PWENT
	BASIC_CFLAGS += -DNO_GECOS_IN_PWENT
endif
ifdef NO_ST_BLOCKS_IN_STRUCT_STAT
	BASIC_CFLAGS += -DNO_ST_BLOCKS_IN_STRUCT_STAT
endif
ifdef USE_NSEC
	BASIC_CFLAGS += -DUSE_NSEC
endif
ifdef USE_ST_TIMESPEC
	BASIC_CFLAGS += -DUSE_ST_TIMESPEC
endif
ifdef NO_NORETURN
	BASIC_CFLAGS += -DNO_NORETURN
endif
ifdef NO_NSEC
	BASIC_CFLAGS += -DNO_NSEC
endif
ifdef SNPRINTF_RETURNS_BOGUS
	COMPAT_CFLAGS += -DSNPRINTF_RETURNS_BOGUS
	COMPAT_OBJS += compat/snprintf.o
endif
ifdef FREAD_READS_DIRECTORIES
	COMPAT_CFLAGS += -DFREAD_READS_DIRECTORIES
	COMPAT_OBJS += compat/fopen.o
endif
ifdef NO_SYMLINK_HEAD
	BASIC_CFLAGS += -DNO_SYMLINK_HEAD
endif
ifdef GETTEXT_POISON
	BASIC_CFLAGS += -DGETTEXT_POISON
endif
ifdef NO_GETTEXT
	BASIC_CFLAGS += -DNO_GETTEXT
	USE_GETTEXT_SCHEME ?= fallthrough
endif
ifdef NO_POLL
	NO_SYS_POLL_H = YesPlease
	COMPAT_CFLAGS += -DNO_POLL -Icompat/poll
	COMPAT_OBJS += compat/poll/poll.o
endif
ifdef NO_STRCASESTR
	COMPAT_CFLAGS += -DNO_STRCASESTR
	COMPAT_OBJS += compat/strcasestr.o
endif
ifdef NO_STRLCPY
	COMPAT_CFLAGS += -DNO_STRLCPY
	COMPAT_OBJS += compat/strlcpy.o
endif
ifdef NO_STRTOUMAX
	COMPAT_CFLAGS += -DNO_STRTOUMAX
	COMPAT_OBJS += compat/strtoumax.o compat/strtoimax.o
endif
ifdef NO_STRTOULL
	COMPAT_CFLAGS += -DNO_STRTOULL
endif
ifdef NO_SETENV
	COMPAT_CFLAGS += -DNO_SETENV
	COMPAT_OBJS += compat/setenv.o
endif
ifdef NO_MKDTEMP
	COMPAT_CFLAGS += -DNO_MKDTEMP
	COMPAT_OBJS += compat/mkdtemp.o
endif
ifdef MKDIR_WO_TRAILING_SLASH
	COMPAT_CFLAGS += -DMKDIR_WO_TRAILING_SLASH
	COMPAT_OBJS += compat/mkdir.o
endif
ifdef NO_MKSTEMPS
	COMPAT_CFLAGS += -DNO_MKSTEMPS
endif
ifdef NO_UNSETENV
	COMPAT_CFLAGS += -DNO_UNSETENV
	COMPAT_OBJS += compat/unsetenv.o
endif
ifdef NO_SYS_SELECT_H
	BASIC_CFLAGS += -DNO_SYS_SELECT_H
endif
ifdef NO_SYS_POLL_H
	BASIC_CFLAGS += -DNO_SYS_POLL_H
endif
ifdef NEEDS_SYS_PARAM_H
	BASIC_CFLAGS += -DNEEDS_SYS_PARAM_H
endif
ifdef NO_INTTYPES_H
	BASIC_CFLAGS += -DNO_INTTYPES_H
endif
ifdef NO_INITGROUPS
	BASIC_CFLAGS += -DNO_INITGROUPS
endif
ifdef NO_MMAP
	COMPAT_CFLAGS += -DNO_MMAP
	COMPAT_OBJS += compat/mmap.o
else
	ifdef USE_WIN32_MMAP
		COMPAT_CFLAGS += -DUSE_WIN32_MMAP
		COMPAT_OBJS += compat/win32mmap.o
	endif
endif
ifdef OBJECT_CREATION_USES_RENAMES
	COMPAT_CFLAGS += -DOBJECT_CREATION_MODE=1
endif
ifdef NO_STRUCT_ITIMERVAL
	COMPAT_CFLAGS += -DNO_STRUCT_ITIMERVAL
	NO_SETITIMER = YesPlease
endif
ifdef NO_SETITIMER
	COMPAT_CFLAGS += -DNO_SETITIMER
endif
ifdef NO_PREAD
	COMPAT_CFLAGS += -DNO_PREAD
	COMPAT_OBJS += compat/pread.o
endif
ifdef NO_FAST_WORKING_DIRECTORY
	BASIC_CFLAGS += -DNO_FAST_WORKING_DIRECTORY
endif
ifdef NO_TRUSTABLE_FILEMODE
	BASIC_CFLAGS += -DNO_TRUSTABLE_FILEMODE
endif
ifdef NO_IPV6
	BASIC_CFLAGS += -DNO_IPV6
endif
ifdef NO_INTPTR_T
	COMPAT_CFLAGS += -DNO_INTPTR_T
endif
ifdef NO_UINTMAX_T
	BASIC_CFLAGS += -Duintmax_t=uint32_t
endif
ifdef NO_SOCKADDR_STORAGE
ifdef NO_IPV6
	BASIC_CFLAGS += -Dsockaddr_storage=sockaddr_in
else
	BASIC_CFLAGS += -Dsockaddr_storage=sockaddr_in6
endif
endif
ifdef NO_INET_NTOP
	LIB_OBJS += compat/inet_ntop.o
	BASIC_CFLAGS += -DNO_INET_NTOP
endif
ifdef NO_INET_PTON
	LIB_OBJS += compat/inet_pton.o
	BASIC_CFLAGS += -DNO_INET_PTON
endif
ifndef NO_UNIX_SOCKETS
	LIB_OBJS += unix-socket.o
	LIB_H += unix-socket.h
	PROGRAM_OBJS += credential-cache.o
	PROGRAM_OBJS += credential-cache--daemon.o
endif

ifdef NO_ICONV
	BASIC_CFLAGS += -DNO_ICONV
endif

ifdef OLD_ICONV
	BASIC_CFLAGS += -DOLD_ICONV
endif

ifdef NO_DEFLATE_BOUND
	BASIC_CFLAGS += -DNO_DEFLATE_BOUND
endif

ifdef NO_POSIX_GOODIES
	BASIC_CFLAGS += -DNO_POSIX_GOODIES
endif

ifdef BLK_SHA1
	SHA1_HEADER = "block-sha1/sha1.h"
	LIB_OBJS += block-sha1/sha1.o
	LIB_H += block-sha1/sha1.h
else
ifdef PPC_SHA1
	SHA1_HEADER = "ppc/sha1.h"
	LIB_OBJS += ppc/sha1.o ppc/sha1ppc.o
	LIB_H += ppc/sha1.h
else
ifdef APPLE_COMMON_CRYPTO
	COMPAT_CFLAGS += -DCOMMON_DIGEST_FOR_OPENSSL
	SHA1_HEADER = <CommonCrypto/CommonDigest.h>
else
	SHA1_HEADER = <openssl/sha.h>
	EXTLIBS += $(LIB_4_CRYPTO)
endif
endif
endif

ifdef NO_PERL_MAKEMAKER
	export NO_PERL_MAKEMAKER
endif
ifdef NO_HSTRERROR
	COMPAT_CFLAGS += -DNO_HSTRERROR
	COMPAT_OBJS += compat/hstrerror.o
endif
ifdef NO_MEMMEM
	COMPAT_CFLAGS += -DNO_MEMMEM
	COMPAT_OBJS += compat/memmem.o
endif
ifdef NO_GETPAGESIZE
	COMPAT_CFLAGS += -DNO_GETPAGESIZE
endif
ifdef INTERNAL_QSORT
	COMPAT_CFLAGS += -DINTERNAL_QSORT
	COMPAT_OBJS += compat/qsort.o
endif
ifdef RUNTIME_PREFIX
	COMPAT_CFLAGS += -DRUNTIME_PREFIX
endif

ifdef NO_PTHREADS
	BASIC_CFLAGS += -DNO_PTHREADS
else
	BASIC_CFLAGS += $(PTHREAD_CFLAGS)
	EXTLIBS += $(PTHREAD_LIBS)
	LIB_OBJS += thread-utils.o
endif

ifdef HAVE_PATHS_H
	BASIC_CFLAGS += -DHAVE_PATHS_H
endif

ifdef HAVE_LIBCHARSET_H
	BASIC_CFLAGS += -DHAVE_LIBCHARSET_H
	EXTLIBS += $(CHARSET_LIB)
endif

ifdef HAVE_STRINGS_H
	BASIC_CFLAGS += -DHAVE_STRINGS_H
endif

ifdef HAVE_DEV_TTY
	BASIC_CFLAGS += -DHAVE_DEV_TTY
endif

ifdef DIR_HAS_BSD_GROUP_SEMANTICS
	COMPAT_CFLAGS += -DDIR_HAS_BSD_GROUP_SEMANTICS
endif
ifdef UNRELIABLE_FSTAT
	BASIC_CFLAGS += -DUNRELIABLE_FSTAT
endif
ifdef NO_REGEX
	COMPAT_CFLAGS += -Icompat/regex
	COMPAT_OBJS += compat/regex/regex.o
endif

ifdef USE_NED_ALLOCATOR
       COMPAT_CFLAGS += -Icompat/nedmalloc
       COMPAT_OBJS += compat/nedmalloc/nedmalloc.o
endif

ifdef GIT_TEST_CMP_USE_COPIED_CONTEXT
	export GIT_TEST_CMP_USE_COPIED_CONTEXT
endif

ifndef NO_MSGFMT_EXTENDED_OPTIONS
	MSGFMT += --check --statistics
endif

ifneq (,$(XDL_FAST_HASH))
	BASIC_CFLAGS += -DXDL_FAST_HASH
endif

ifdef GMTIME_UNRELIABLE_ERRORS
	COMPAT_OBJS += compat/gmtime.o
	BASIC_CFLAGS += -DGMTIME_UNRELIABLE_ERRORS
endif

ifdef HAVE_CLOCK_GETTIME
	BASIC_CFLAGS += -DHAVE_CLOCK_GETTIME
	EXTLIBS += -lrt
endif

ifeq ($(TCLTK_PATH),)
NO_TCLTK = NoThanks
endif

ifeq ($(PERL_PATH),)
NO_PERL = NoThanks
endif

ifeq ($(PYTHON_PATH),)
NO_PYTHON = NoThanks
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
	QUIET_CC       = @echo '   ' CC $@;
	QUIET_AR       = @echo '   ' AR $@;
	QUIET_LINK     = @echo '   ' LINK $@;
	QUIET_BUILT_IN = @echo '   ' BUILTIN $@;
	QUIET_GEN      = @echo '   ' GEN $@;
	QUIET_LNCP     = @echo '   ' LN/CP $@;
	QUIET_XGETTEXT = @echo '   ' XGETTEXT $@;
	QUIET_MSGFMT   = @echo '   ' MSGFMT $@;
	QUIET_GCOV     = @echo '   ' GCOV $@;
	QUIET_SP       = @echo '   ' SP $<;
	QUIET_RC       = @echo '   ' RC $@;
	QUIET_SUBDIR0  = +@subdir=
	QUIET_SUBDIR1  = ;$(NO_SUBDIR) echo '   ' SUBDIR $$subdir; \
			 $(MAKE) $(PRINT_DIR) -C $$subdir
	export V
	export QUIET_GEN
	export QUIET_BUILT_IN
endif
endif

ifdef NO_INSTALL_HARDLINKS
	export NO_INSTALL_HARDLINKS
endif

### profile feedback build
#

# Can adjust this to be a global directory if you want to do extended
# data gathering
PROFILE_DIR := $(CURDIR)

ifeq ("$(PROFILE)","GEN")
	BASIC_CFLAGS += -fprofile-generate=$(PROFILE_DIR) -DNO_NORETURN=1
	EXTLIBS += -lgcov
	export CCACHE_DISABLE = t
	V = 1
else
ifneq ("$(PROFILE)","")
	BASIC_CFLAGS += -fprofile-use=$(PROFILE_DIR) -fprofile-correction -DNO_NORETURN=1
	export CCACHE_DISABLE = t
	V = 1
endif
endif

# Shell quote (do not use $(call) to accommodate ancient setups);

SHA1_HEADER_SQ = $(subst ','\'',$(SHA1_HEADER))
ETC_GITCONFIG_SQ = $(subst ','\'',$(ETC_GITCONFIG))
ETC_GITATTRIBUTES_SQ = $(subst ','\'',$(ETC_GITATTRIBUTES))

DESTDIR_SQ = $(subst ','\'',$(DESTDIR))
bindir_SQ = $(subst ','\'',$(bindir))
bindir_relative_SQ = $(subst ','\'',$(bindir_relative))
mandir_relative_SQ = $(subst ','\'',$(mandir_relative))
infodir_relative_SQ = $(subst ','\'',$(infodir_relative))
localedir_SQ = $(subst ','\'',$(localedir))
gitexecdir_SQ = $(subst ','\'',$(gitexecdir))
template_dir_SQ = $(subst ','\'',$(template_dir))
htmldir_relative_SQ = $(subst ','\'',$(htmldir_relative))
prefix_SQ = $(subst ','\'',$(prefix))
gitwebdir_SQ = $(subst ','\'',$(gitwebdir))

SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))
PERL_PATH_SQ = $(subst ','\'',$(PERL_PATH))
PYTHON_PATH_SQ = $(subst ','\'',$(PYTHON_PATH))
TCLTK_PATH_SQ = $(subst ','\'',$(TCLTK_PATH))
DIFF_SQ = $(subst ','\'',$(DIFF))
PERLLIB_EXTRA_SQ = $(subst ','\'',$(PERLLIB_EXTRA))

LIBS = $(GITLIBS) $(EXTLIBS)

BASIC_CFLAGS += -DSHA1_HEADER='$(SHA1_HEADER_SQ)' \
	$(COMPAT_CFLAGS)
LIB_OBJS += $(COMPAT_OBJS)

# Quote for C

ifdef DEFAULT_EDITOR
DEFAULT_EDITOR_CQ = "$(subst ",\",$(subst \,\\,$(DEFAULT_EDITOR)))"
DEFAULT_EDITOR_CQ_SQ = $(subst ','\'',$(DEFAULT_EDITOR_CQ))

BASIC_CFLAGS += -DDEFAULT_EDITOR='$(DEFAULT_EDITOR_CQ_SQ)'
endif

ifdef DEFAULT_PAGER
DEFAULT_PAGER_CQ = "$(subst ",\",$(subst \,\\,$(DEFAULT_PAGER)))"
DEFAULT_PAGER_CQ_SQ = $(subst ','\'',$(DEFAULT_PAGER_CQ))

BASIC_CFLAGS += -DDEFAULT_PAGER='$(DEFAULT_PAGER_CQ_SQ)'
endif

ifdef SHELL_PATH
SHELL_PATH_CQ = "$(subst ",\",$(subst \,\\,$(SHELL_PATH)))"
SHELL_PATH_CQ_SQ = $(subst ','\'',$(SHELL_PATH_CQ))

BASIC_CFLAGS += -DSHELL_PATH='$(SHELL_PATH_CQ_SQ)'
endif

GIT_USER_AGENT_SQ = $(subst ','\'',$(GIT_USER_AGENT))
GIT_USER_AGENT_CQ = "$(subst ",\",$(subst \,\\,$(GIT_USER_AGENT)))"
GIT_USER_AGENT_CQ_SQ = $(subst ','\'',$(GIT_USER_AGENT_CQ))
GIT-USER-AGENT: FORCE
	@if test x'$(GIT_USER_AGENT_SQ)' != x"`cat GIT-USER-AGENT 2>/dev/null`"; then \
		echo '$(GIT_USER_AGENT_SQ)' >GIT-USER-AGENT; \
	fi

ifdef DEFAULT_HELP_FORMAT
BASIC_CFLAGS += -DDEFAULT_HELP_FORMAT='"$(DEFAULT_HELP_FORMAT)"'
endif

ALL_CFLAGS += $(BASIC_CFLAGS)
ALL_LDFLAGS += $(BASIC_LDFLAGS)

export DIFF TAR INSTALL DESTDIR SHELL_PATH


### Build rules

SHELL = $(SHELL_PATH)

all:: shell_compatibility_test

ifeq "$(PROFILE)" "BUILD"
all:: profile
endif

profile:: profile-clean
	$(MAKE) PROFILE=GEN all
	$(MAKE) PROFILE=GEN -j1 test
	$(MAKE) PROFILE=GEN -j1 perf
	$(MAKE) PROFILE=USE all

profile-fast: profile-clean
	$(MAKE) PROFILE=GEN all
	$(MAKE) PROFILE=GEN -j1 perf
	$(MAKE) PROFILE=USE all


all:: $(ALL_PROGRAMS) $(SCRIPT_LIB) $(BUILT_INS) $(OTHER_PROGRAMS) GIT-BUILD-OPTIONS
ifneq (,$X)
	$(QUIET_BUILT_IN)$(foreach p,$(patsubst %$X,%,$(filter %$X,$(ALL_PROGRAMS) $(BUILT_INS) git$X)), test -d '$p' -o '$p' -ef '$p$X' || $(RM) '$p';)
endif

all::
ifndef NO_TCLTK
	$(QUIET_SUBDIR0)git-gui $(QUIET_SUBDIR1) gitexecdir='$(gitexec_instdir_SQ)' all
	$(QUIET_SUBDIR0)gitk-git $(QUIET_SUBDIR1) all
endif
ifndef NO_PERL
	$(QUIET_SUBDIR0)perl $(QUIET_SUBDIR1) PERL_PATH='$(PERL_PATH_SQ)' prefix='$(prefix_SQ)' localedir='$(localedir_SQ)' all
endif
	$(QUIET_SUBDIR0)templates $(QUIET_SUBDIR1) SHELL_PATH='$(SHELL_PATH_SQ)' PERL_PATH='$(PERL_PATH_SQ)'

please_set_SHELL_PATH_to_a_more_modern_shell:
	@$$(:)

shell_compatibility_test: please_set_SHELL_PATH_to_a_more_modern_shell

strip: $(PROGRAMS) git$X
	$(STRIP) $(STRIP_OPTS) $^

### Target-specific flags and dependencies

# The generic compilation pattern rule and automatically
# computed header dependencies (falling back to a dependency on
# LIB_H) are enough to describe how most targets should be built,
# but some targets are special enough to need something a little
# different.
#
# - When a source file "foo.c" #includes a generated header file,
#   we need to list that dependency for the "foo.o" target.
#
#   We also list it from other targets that are built from foo.c
#   like "foo.sp" and "foo.s", even though that is easy to forget
#   to do because the generated header is already present around
#   after a regular build attempt.
#
# - Some code depends on configuration kept in makefile
#   variables. The target-specific variable EXTRA_CPPFLAGS can
#   be used to convey that information to the C preprocessor
#   using -D options.
#
#   The "foo.o" target should have a corresponding dependency on
#   a file that changes when the value of the makefile variable
#   changes.  For example, targets making use of the
#   $(GIT_VERSION) variable depend on GIT-VERSION-FILE.
#
#   Technically the ".sp" and ".s" targets do not need this
#   dependency because they are force-built, but they get the
#   same dependency for consistency. This way, you do not have to
#   know how each target is implemented. And it means the
#   dependencies here will not need to change if the force-build
#   details change some day.

git.sp git.s git.o: GIT-PREFIX
git.sp git.s git.o: EXTRA_CPPFLAGS = \
	'-DGIT_HTML_PATH="$(htmldir_relative_SQ)"' \
	'-DGIT_MAN_PATH="$(mandir_relative_SQ)"' \
	'-DGIT_INFO_PATH="$(infodir_relative_SQ)"'

git$X: git.o GIT-LDFLAGS $(BUILTIN_OBJS) $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ git.o \
		$(BUILTIN_OBJS) $(ALL_LDFLAGS) $(LIBS)

help.sp help.s help.o: common-cmds.h

builtin/help.sp builtin/help.s builtin/help.o: common-cmds.h GIT-PREFIX
builtin/help.sp builtin/help.s builtin/help.o: EXTRA_CPPFLAGS = \
	'-DGIT_HTML_PATH="$(htmldir_relative_SQ)"' \
	'-DGIT_MAN_PATH="$(mandir_relative_SQ)"' \
	'-DGIT_INFO_PATH="$(infodir_relative_SQ)"'

version.sp version.s version.o: GIT-VERSION-FILE GIT-USER-AGENT
version.sp version.s version.o: EXTRA_CPPFLAGS = \
	'-DGIT_VERSION="$(GIT_VERSION)"' \
	'-DGIT_USER_AGENT=$(GIT_USER_AGENT_CQ_SQ)'

$(BUILT_INS): git$X
	$(QUIET_BUILT_IN)$(RM) $@ && \
	ln $< $@ 2>/dev/null || \
	ln -s $< $@ 2>/dev/null || \
	cp $< $@

common-cmds.h: ./generate-cmdlist.sh command-list.txt

common-cmds.h: $(wildcard Documentation/git-*.txt)
	$(QUIET_GEN)./generate-cmdlist.sh > $@+ && mv $@+ $@

SCRIPT_DEFINES = $(SHELL_PATH_SQ):$(DIFF_SQ):$(GIT_VERSION):\
	$(localedir_SQ):$(NO_CURL):$(USE_GETTEXT_SCHEME):$(SANE_TOOL_PATH_SQ):\
	$(gitwebdir_SQ):$(PERL_PATH_SQ)
define cmd_munge_script
$(RM) $@ $@+ && \
sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
    -e 's|@SHELL_PATH@|$(SHELL_PATH_SQ)|' \
    -e 's|@@DIFF@@|$(DIFF_SQ)|' \
    -e 's|@@LOCALEDIR@@|$(localedir_SQ)|g' \
    -e 's/@@NO_CURL@@/$(NO_CURL)/g' \
    -e 's/@@USE_GETTEXT_SCHEME@@/$(USE_GETTEXT_SCHEME)/g' \
    -e $(BROKEN_PATH_FIX) \
    -e 's|@@GITWEBDIR@@|$(gitwebdir_SQ)|g' \
    -e 's|@@PERL@@|$(PERL_PATH_SQ)|g' \
    $@.sh >$@+
endef

GIT-SCRIPT-DEFINES: FORCE
	@FLAGS='$(SCRIPT_DEFINES)'; \
	    if test x"$$FLAGS" != x"`cat $@ 2>/dev/null`" ; then \
		echo >&2 "    * new script parameters"; \
		echo "$$FLAGS" >$@; \
            fi


$(patsubst %.sh,%,$(SCRIPT_SH)) : % : %.sh GIT-SCRIPT-DEFINES
	$(QUIET_GEN)$(cmd_munge_script) && \
	chmod +x $@+ && \
	mv $@+ $@

$(SCRIPT_LIB) : % : %.sh GIT-SCRIPT-DEFINES
	$(QUIET_GEN)$(cmd_munge_script) && \
	mv $@+ $@

git.res: git.rc GIT-VERSION-FILE
	$(QUIET_RC)$(RC) \
	  $(join -DMAJOR= -DMINOR=, $(wordlist 1,2,$(subst -, ,$(subst ., ,$(GIT_VERSION))))) \
	  -DGIT_VERSION="\\\"$(GIT_VERSION)\\\"" $< -o $@

ifndef NO_PERL
$(patsubst %.perl,%,$(SCRIPT_PERL)): perl/perl.mak

perl/perl.mak: perl/PM.stamp

perl/PM.stamp: FORCE
	$(QUIET_GEN)$(FIND) perl -type f -name '*.pm' | sort >$@+ && \
	{ cmp $@+ $@ >/dev/null 2>/dev/null || mv $@+ $@; } && \
	$(RM) $@+

perl/perl.mak: GIT-CFLAGS GIT-PREFIX perl/Makefile perl/Makefile.PL
	$(QUIET_SUBDIR0)perl $(QUIET_SUBDIR1) PERL_PATH='$(PERL_PATH_SQ)' prefix='$(prefix_SQ)' $(@F)

PERL_DEFINES = $(PERL_PATH_SQ):$(PERLLIB_EXTRA_SQ)
$(patsubst %.perl,%,$(SCRIPT_PERL)): % : %.perl perl/perl.mak GIT-PERL-DEFINES GIT-VERSION-FILE
	$(QUIET_GEN)$(RM) $@ $@+ && \
	INSTLIBDIR=`MAKEFLAGS= $(MAKE) -C perl -s --no-print-directory instlibdir` && \
	INSTLIBDIR_EXTRA='$(PERLLIB_EXTRA_SQ)' && \
	INSTLIBDIR="$$INSTLIBDIR$${INSTLIBDIR_EXTRA:+:$$INSTLIBDIR_EXTRA}" && \
	sed -e '1{' \
	    -e '	s|#!.*perl|#!$(PERL_PATH_SQ)|' \
	    -e '	h' \
	    -e '	s=.*=use lib (split(/$(pathsep)/, $$ENV{GITPERLLIB} || "'"$$INSTLIBDIR"'"));=' \
	    -e '	H' \
	    -e '	x' \
	    -e '}' \
	    -e 's/@@GIT_VERSION@@/$(GIT_VERSION)/g' \
	    $< >$@+ && \
	chmod +x $@+ && \
	mv $@+ $@

GIT-PERL-DEFINES: FORCE
	@FLAGS='$(PERL_DEFINES)'; \
	    if test x"$$FLAGS" != x"`cat $@ 2>/dev/null`" ; then \
		echo >&2 "    * new perl-specific parameters"; \
		echo "$$FLAGS" >$@; \
	    fi


.PHONY: gitweb
gitweb:
	$(QUIET_SUBDIR0)gitweb $(QUIET_SUBDIR1) all

git-instaweb: git-instaweb.sh gitweb GIT-SCRIPT-DEFINES
	$(QUIET_GEN)$(cmd_munge_script) && \
	chmod +x $@+ && \
	mv $@+ $@
else # NO_PERL
$(patsubst %.perl,%,$(SCRIPT_PERL)) git-instaweb: % : unimplemented.sh
	$(QUIET_GEN)$(RM) $@ $@+ && \
	sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
	    -e 's|@@REASON@@|NO_PERL=$(NO_PERL)|g' \
	    unimplemented.sh >$@+ && \
	chmod +x $@+ && \
	mv $@+ $@
endif # NO_PERL

ifndef NO_PYTHON
$(SCRIPT_PYTHON_GEN): GIT-CFLAGS GIT-PREFIX GIT-PYTHON-VARS
$(SCRIPT_PYTHON_GEN): % : %.py
	$(QUIET_GEN)$(RM) $@ $@+ && \
	sed -e '1s|#!.*python|#!$(PYTHON_PATH_SQ)|' \
	    $< >$@+ && \
	chmod +x $@+ && \
	mv $@+ $@
else # NO_PYTHON
$(SCRIPT_PYTHON_GEN): % : unimplemented.sh
	$(QUIET_GEN)$(RM) $@ $@+ && \
	sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
	    -e 's|@@REASON@@|NO_PYTHON=$(NO_PYTHON)|g' \
	    unimplemented.sh >$@+ && \
	chmod +x $@+ && \
	mv $@+ $@
endif # NO_PYTHON

CONFIGURE_RECIPE = $(RM) configure configure.ac+ && \
		   sed -e 's/@@GIT_VERSION@@/$(GIT_VERSION)/g' \
			configure.ac >configure.ac+ && \
		   autoconf -o configure configure.ac+ && \
		   $(RM) configure.ac+

configure: configure.ac GIT-VERSION-FILE
	$(QUIET_GEN)$(CONFIGURE_RECIPE)

ifdef AUTOCONFIGURED
# We avoid depending on 'configure' here, because it gets rebuilt
# every time GIT-VERSION-FILE is modified, only to update the embedded
# version number string, which config.status does not care about.  We
# do want to recheck when the platform/environment detection logic
# changes, hence this depends on configure.ac.
config.status: configure.ac
	$(QUIET_GEN)$(CONFIGURE_RECIPE) && \
	if test -f config.status; then \
	  ./config.status --recheck; \
	else \
	  ./configure; \
	fi
reconfigure config.mak.autogen: config.status
	$(QUIET_GEN)./config.status
.PHONY: reconfigure # This is a convenience target.
endif

XDIFF_OBJS += xdiff/xdiffi.o
XDIFF_OBJS += xdiff/xprepare.o
XDIFF_OBJS += xdiff/xutils.o
XDIFF_OBJS += xdiff/xemit.o
XDIFF_OBJS += xdiff/xmerge.o
XDIFF_OBJS += xdiff/xpatience.o
XDIFF_OBJS += xdiff/xhistogram.o

VCSSVN_OBJS += vcs-svn/line_buffer.o
VCSSVN_OBJS += vcs-svn/sliding_window.o
VCSSVN_OBJS += vcs-svn/repo_tree.o
VCSSVN_OBJS += vcs-svn/fast_export.o
VCSSVN_OBJS += vcs-svn/svndiff.o
VCSSVN_OBJS += vcs-svn/svndump.o

TEST_OBJS := $(patsubst test-%$X,test-%.o,$(TEST_PROGRAMS))
OBJECTS := $(LIB_OBJS) $(BUILTIN_OBJS) $(PROGRAM_OBJS) $(TEST_OBJS) \
	$(XDIFF_OBJS) \
	$(VCSSVN_OBJS) \
	git.o
ifndef NO_CURL
	OBJECTS += http.o http-walker.o remote-curl.o
endif

dep_files := $(foreach f,$(OBJECTS),$(dir $f).depend/$(notdir $f).d)
dep_dirs := $(addsuffix .depend,$(sort $(dir $(OBJECTS))))

ifeq ($(COMPUTE_HEADER_DEPENDENCIES),yes)
$(dep_dirs):
	@mkdir -p $@

missing_dep_dirs := $(filter-out $(wildcard $(dep_dirs)),$(dep_dirs))
dep_file = $(dir $@).depend/$(notdir $@).d
dep_args = -MF $(dep_file) -MQ $@ -MMD -MP
ifdef CHECK_HEADER_DEPENDENCIES
$(error cannot compute header dependencies outside a normal build. \
Please unset CHECK_HEADER_DEPENDENCIES and try again)
endif
endif

ifneq ($(COMPUTE_HEADER_DEPENDENCIES),yes)
ifndef CHECK_HEADER_DEPENDENCIES
dep_dirs =
missing_dep_dirs =
dep_args =
endif
endif

ifdef CHECK_HEADER_DEPENDENCIES
ifndef PRINT_HEADER_DEPENDENCIES
missing_deps = $(filter-out $(notdir $^), \
	$(notdir $(shell $(MAKE) -s $@ \
		CHECK_HEADER_DEPENDENCIES=YesPlease \
		USE_COMPUTED_HEADER_DEPENDENCIES=YesPlease \
		PRINT_HEADER_DEPENDENCIES=YesPlease)))
endif
endif

ASM_SRC := $(wildcard $(OBJECTS:o=S))
ASM_OBJ := $(ASM_SRC:S=o)
C_OBJ := $(filter-out $(ASM_OBJ),$(OBJECTS))

.SUFFIXES:

ifdef PRINT_HEADER_DEPENDENCIES
$(C_OBJ): %.o: %.c FORCE
	echo $^
$(ASM_OBJ): %.o: %.S FORCE
	echo $^

ifndef CHECK_HEADER_DEPENDENCIES
$(error cannot print header dependencies during a normal build. \
Please set CHECK_HEADER_DEPENDENCIES and try again)
endif
endif

ifndef PRINT_HEADER_DEPENDENCIES
ifdef CHECK_HEADER_DEPENDENCIES
$(C_OBJ): %.o: %.c $(dep_files) FORCE
	@set -e; echo CHECK $@; \
	missing_deps="$(missing_deps)"; \
	if test "$$missing_deps"; \
	then \
		echo missing dependencies: $$missing_deps; \
		false; \
	fi
$(ASM_OBJ): %.o: %.S $(dep_files) FORCE
	@set -e; echo CHECK $@; \
	missing_deps="$(missing_deps)"; \
	if test "$$missing_deps"; \
	then \
		echo missing dependencies: $$missing_deps; \
		false; \
	fi
endif
endif

ifndef CHECK_HEADER_DEPENDENCIES
$(C_OBJ): %.o: %.c GIT-CFLAGS $(missing_dep_dirs)
	$(QUIET_CC)$(CC) -o $*.o -c $(dep_args) $(ALL_CFLAGS) $(EXTRA_CPPFLAGS) $<
$(ASM_OBJ): %.o: %.S GIT-CFLAGS $(missing_dep_dirs)
	$(QUIET_CC)$(CC) -o $*.o -c $(dep_args) $(ALL_CFLAGS) $(EXTRA_CPPFLAGS) $<
endif

%.s: %.c GIT-CFLAGS FORCE
	$(QUIET_CC)$(CC) -o $@ -S $(ALL_CFLAGS) $(EXTRA_CPPFLAGS) $<

ifdef USE_COMPUTED_HEADER_DEPENDENCIES
# Take advantage of gcc's on-the-fly dependency generation
# See <http://gcc.gnu.org/gcc-3.0/features.html>.
dep_files_present := $(wildcard $(dep_files))
ifneq ($(dep_files_present),)
include $(dep_files_present)
endif
else
# Dependencies on header files, for platforms that do not support
# the gcc -MMD option.
#
# Dependencies on automatically generated headers such as common-cmds.h
# should _not_ be included here, since they are necessary even when
# building an object for the first time.

$(OBJECTS): $(LIB_H)
endif

exec_cmd.sp exec_cmd.s exec_cmd.o: GIT-PREFIX
exec_cmd.sp exec_cmd.s exec_cmd.o: EXTRA_CPPFLAGS = \
	'-DGIT_EXEC_PATH="$(gitexecdir_SQ)"' \
	'-DBINDIR="$(bindir_relative_SQ)"' \
	'-DPREFIX="$(prefix_SQ)"'

builtin/init-db.sp builtin/init-db.s builtin/init-db.o: GIT-PREFIX
builtin/init-db.sp builtin/init-db.s builtin/init-db.o: EXTRA_CPPFLAGS = \
	-DDEFAULT_GIT_TEMPLATE_DIR='"$(template_dir_SQ)"'

config.sp config.s config.o: GIT-PREFIX
config.sp config.s config.o: EXTRA_CPPFLAGS = \
	-DETC_GITCONFIG='"$(ETC_GITCONFIG_SQ)"'

attr.sp attr.s attr.o: GIT-PREFIX
attr.sp attr.s attr.o: EXTRA_CPPFLAGS = \
	-DETC_GITATTRIBUTES='"$(ETC_GITATTRIBUTES_SQ)"'

gettext.sp gettext.s gettext.o: GIT-PREFIX
gettext.sp gettext.s gettext.o: EXTRA_CPPFLAGS = \
	-DGIT_LOCALE_PATH='"$(localedir_SQ)"'

http-push.sp http.sp http-walker.sp remote-curl.sp: SPARSE_FLAGS += \
	-DCURL_DISABLE_TYPECHECK

ifdef NO_EXPAT
http-walker.sp http-walker.s http-walker.o: EXTRA_CPPFLAGS = -DNO_EXPAT
endif

ifdef NO_REGEX
compat/regex/regex.sp compat/regex/regex.o: EXTRA_CPPFLAGS = \
	-DGAWK -DNO_MBSUPPORT
endif

ifdef USE_NED_ALLOCATOR
compat/nedmalloc/nedmalloc.sp compat/nedmalloc/nedmalloc.o: EXTRA_CPPFLAGS = \
	-DNDEBUG -DOVERRIDE_STRDUP -DREPLACE_SYSTEM_ALLOCATOR
compat/nedmalloc/nedmalloc.sp: SPARSE_FLAGS += -Wno-non-pointer-null
endif

git-%$X: %.o GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) $(LIBS)

git-imap-send$X: imap-send.o GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) \
		$(LIBS) $(OPENSSL_LINK) $(OPENSSL_LIBSSL) $(LIB_4_CRYPTO)

git-http-fetch$X: http.o http-walker.o http-fetch.o GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) \
		$(LIBS) $(CURL_LIBCURL)
git-http-push$X: http.o http-push.o GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) \
		$(LIBS) $(CURL_LIBCURL) $(EXPAT_LIBEXPAT)

git-remote-testsvn$X: remote-testsvn.o GIT-LDFLAGS $(GITLIBS) $(VCSSVN_LIB)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) $(LIBS) \
	$(VCSSVN_LIB)

$(REMOTE_CURL_ALIASES): $(REMOTE_CURL_PRIMARY)
	$(QUIET_LNCP)$(RM) $@ && \
	ln $< $@ 2>/dev/null || \
	ln -s $< $@ 2>/dev/null || \
	cp $< $@

$(REMOTE_CURL_PRIMARY): remote-curl.o http.o http-walker.o GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) \
		$(LIBS) $(CURL_LIBCURL) $(EXPAT_LIBEXPAT)

$(LIB_FILE): $(LIB_OBJS)
	$(QUIET_AR)$(RM) $@ && $(AR) rcs $@ $^

$(XDIFF_LIB): $(XDIFF_OBJS)
	$(QUIET_AR)$(RM) $@ && $(AR) rcs $@ $^

$(VCSSVN_LIB): $(VCSSVN_OBJS)
	$(QUIET_AR)$(RM) $@ && $(AR) rcs $@ $^

export DEFAULT_EDITOR DEFAULT_PAGER

doc:
	$(MAKE) -C Documentation all

man:
	$(MAKE) -C Documentation man

html:
	$(MAKE) -C Documentation html

info:
	$(MAKE) -C Documentation info

pdf:
	$(MAKE) -C Documentation pdf

XGETTEXT_FLAGS = \
	--force-po \
	--add-comments=TRANSLATORS: \
	--msgid-bugs-address="Git Mailing List <git@vger.kernel.org>" \
	--from-code=UTF-8
XGETTEXT_FLAGS_C = $(XGETTEXT_FLAGS) --language=C \
	--keyword=_ --keyword=N_ --keyword="Q_:1,2"
XGETTEXT_FLAGS_SH = $(XGETTEXT_FLAGS) --language=Shell \
	--keyword=gettextln --keyword=eval_gettextln
XGETTEXT_FLAGS_PERL = $(XGETTEXT_FLAGS) --keyword=__ --language=Perl
LOCALIZED_C := $(C_OBJ:o=c) $(LIB_H) $(GENERATED_H)
LOCALIZED_SH := $(SCRIPT_SH)
LOCALIZED_PERL := $(SCRIPT_PERL)

ifdef XGETTEXT_INCLUDE_TESTS
LOCALIZED_C += t/t0200/test.c
LOCALIZED_SH += t/t0200/test.sh
LOCALIZED_PERL += t/t0200/test.perl
endif

po/git.pot: $(LOCALIZED_C)
	$(QUIET_XGETTEXT)$(XGETTEXT) -o$@+ $(XGETTEXT_FLAGS_C) $(LOCALIZED_C)
	$(QUIET_XGETTEXT)$(XGETTEXT) -o$@+ --join-existing $(XGETTEXT_FLAGS_SH) \
		$(LOCALIZED_SH)
	$(QUIET_XGETTEXT)$(XGETTEXT) -o$@+ --join-existing $(XGETTEXT_FLAGS_PERL) \
		$(LOCALIZED_PERL)
	mv $@+ $@

pot: po/git.pot

POFILES := $(wildcard po/*.po)
MOFILES := $(patsubst po/%.po,po/build/locale/%/LC_MESSAGES/git.mo,$(POFILES))

ifndef NO_GETTEXT
all:: $(MOFILES)
endif

po/build/locale/%/LC_MESSAGES/git.mo: po/%.po
	$(QUIET_MSGFMT)mkdir -p $(dir $@) && $(MSGFMT) -o $@ $<

FIND_SOURCE_FILES = ( git ls-files '*.[hcS]' 2>/dev/null || \
			$(FIND) . \( -name .git -type d -prune \) \
				-o \( -name '*.[hcS]' -type f -print \) )

$(ETAGS_TARGET): FORCE
	$(RM) $(ETAGS_TARGET)
	$(FIND_SOURCE_FILES) | xargs etags -a -o $(ETAGS_TARGET)

tags: FORCE
	$(RM) tags
	$(FIND_SOURCE_FILES) | xargs ctags -a

cscope:
	$(RM) cscope*
	$(FIND_SOURCE_FILES) | xargs cscope -b

### Detect prefix changes
TRACK_PREFIX = $(bindir_SQ):$(gitexecdir_SQ):$(template_dir_SQ):$(prefix_SQ):\
		$(localedir_SQ)

GIT-PREFIX: FORCE
	@FLAGS='$(TRACK_PREFIX)'; \
	if test x"$$FLAGS" != x"`cat GIT-PREFIX 2>/dev/null`" ; then \
		echo >&2 "    * new prefix flags"; \
		echo "$$FLAGS" >GIT-PREFIX; \
	fi

TRACK_CFLAGS = $(CC):$(subst ','\'',$(ALL_CFLAGS)):$(USE_GETTEXT_SCHEME)

GIT-CFLAGS: FORCE
	@FLAGS='$(TRACK_CFLAGS)'; \
	    if test x"$$FLAGS" != x"`cat GIT-CFLAGS 2>/dev/null`" ; then \
		echo >&2 "    * new build flags"; \
		echo "$$FLAGS" >GIT-CFLAGS; \
            fi

TRACK_LDFLAGS = $(subst ','\'',$(ALL_LDFLAGS))

GIT-LDFLAGS: FORCE
	@FLAGS='$(TRACK_LDFLAGS)'; \
	    if test x"$$FLAGS" != x"`cat GIT-LDFLAGS 2>/dev/null`" ; then \
		echo >&2 "    * new link flags"; \
		echo "$$FLAGS" >GIT-LDFLAGS; \
            fi

# We need to apply sq twice, once to protect from the shell
# that runs GIT-BUILD-OPTIONS, and then again to protect it
# and the first level quoting from the shell that runs "echo".
GIT-BUILD-OPTIONS: FORCE
	@echo SHELL_PATH=\''$(subst ','\'',$(SHELL_PATH_SQ))'\' >$@
	@echo PERL_PATH=\''$(subst ','\'',$(PERL_PATH_SQ))'\' >>$@
	@echo DIFF=\''$(subst ','\'',$(subst ','\'',$(DIFF)))'\' >>$@
	@echo PYTHON_PATH=\''$(subst ','\'',$(PYTHON_PATH_SQ))'\' >>$@
	@echo TAR=\''$(subst ','\'',$(subst ','\'',$(TAR)))'\' >>$@
	@echo NO_CURL=\''$(subst ','\'',$(subst ','\'',$(NO_CURL)))'\' >>$@
	@echo USE_LIBPCRE=\''$(subst ','\'',$(subst ','\'',$(USE_LIBPCRE)))'\' >>$@
	@echo NO_PERL=\''$(subst ','\'',$(subst ','\'',$(NO_PERL)))'\' >>$@
	@echo NO_PYTHON=\''$(subst ','\'',$(subst ','\'',$(NO_PYTHON)))'\' >>$@
	@echo NO_UNIX_SOCKETS=\''$(subst ','\'',$(subst ','\'',$(NO_UNIX_SOCKETS)))'\' >>$@
ifdef TEST_OUTPUT_DIRECTORY
	@echo TEST_OUTPUT_DIRECTORY=\''$(subst ','\'',$(subst ','\'',$(TEST_OUTPUT_DIRECTORY)))'\' >>$@
endif
ifdef GIT_TEST_OPTS
	@echo GIT_TEST_OPTS=\''$(subst ','\'',$(subst ','\'',$(GIT_TEST_OPTS)))'\' >>$@
endif
ifdef GIT_TEST_CMP
	@echo GIT_TEST_CMP=\''$(subst ','\'',$(subst ','\'',$(GIT_TEST_CMP)))'\' >>$@
endif
ifdef GIT_TEST_CMP_USE_COPIED_CONTEXT
	@echo GIT_TEST_CMP_USE_COPIED_CONTEXT=YesPlease >>$@
endif
	@echo NO_GETTEXT=\''$(subst ','\'',$(subst ','\'',$(NO_GETTEXT)))'\' >>$@
	@echo GETTEXT_POISON=\''$(subst ','\'',$(subst ','\'',$(GETTEXT_POISON)))'\' >>$@
ifdef GIT_PERF_REPEAT_COUNT
	@echo GIT_PERF_REPEAT_COUNT=\''$(subst ','\'',$(subst ','\'',$(GIT_PERF_REPEAT_COUNT)))'\' >>$@
endif
ifdef GIT_PERF_REPO
	@echo GIT_PERF_REPO=\''$(subst ','\'',$(subst ','\'',$(GIT_PERF_REPO)))'\' >>$@
endif
ifdef GIT_PERF_LARGE_REPO
	@echo GIT_PERF_LARGE_REPO=\''$(subst ','\'',$(subst ','\'',$(GIT_PERF_LARGE_REPO)))'\' >>$@
endif
ifdef GIT_PERF_MAKE_OPTS
	@echo GIT_PERF_MAKE_OPTS=\''$(subst ','\'',$(subst ','\'',$(GIT_PERF_MAKE_OPTS)))'\' >>$@
endif
ifdef TEST_GIT_INDEX_VERSION
	@echo TEST_GIT_INDEX_VERSION=\''$(subst ','\'',$(subst ','\'',$(TEST_GIT_INDEX_VERSION)))'\' >>$@
endif

### Detect Python interpreter path changes
ifndef NO_PYTHON
TRACK_PYTHON = $(subst ','\'',-DPYTHON_PATH='$(PYTHON_PATH_SQ)')

GIT-PYTHON-VARS: FORCE
	@VARS='$(TRACK_PYTHON)'; \
	    if test x"$$VARS" != x"`cat $@ 2>/dev/null`" ; then \
		echo >&2 "    * new Python interpreter location"; \
		echo "$$VARS" >$@; \
            fi
endif

test_bindir_programs := $(patsubst %,bin-wrappers/%,$(BINDIR_PROGRAMS_NEED_X) $(BINDIR_PROGRAMS_NO_X) $(TEST_PROGRAMS_NEED_X))

all:: $(TEST_PROGRAMS) $(test_bindir_programs)
all:: $(NO_INSTALL)

bin-wrappers/%: wrap-for-bin.sh
	@mkdir -p bin-wrappers
	$(QUIET_GEN)sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
	     -e 's|@@BUILD_DIR@@|$(shell pwd)|' \
	     -e 's|@@PROG@@|$(@F)|' < $< > $@ && \
	chmod +x $@

# GNU make supports exporting all variables by "export" without parameters.
# However, the environment gets quite big, and some programs have problems
# with that.

export NO_SVN_TESTS
export TEST_NO_MALLOC_CHECK

### Testing rules

test: all
	$(MAKE) -C t/ all

perf: all
	$(MAKE) -C t/perf/ all

.PHONY: test perf

test-ctype$X: ctype.o

test-date$X: date.o ctype.o

test-delta$X: diff-delta.o patch-delta.o

test-line-buffer$X: vcs-svn/lib.a

test-parse-options$X: parse-options.o parse-options-cb.o

test-svn-fe$X: vcs-svn/lib.a

.PRECIOUS: $(TEST_OBJS)

test-%$X: test-%.o GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) $(filter %.a,$^) $(LIBS)

check-sha1:: test-sha1$X
	./test-sha1.sh

SP_OBJ = $(patsubst %.o,%.sp,$(C_OBJ))

$(SP_OBJ): %.sp: %.c GIT-CFLAGS FORCE
	$(QUIET_SP)cgcc -no-compile $(ALL_CFLAGS) $(EXTRA_CPPFLAGS) \
		$(SPARSE_FLAGS) $<

.PHONY: sparse $(SP_OBJ)
sparse: $(SP_OBJ)

check: common-cmds.h
	@if sparse; \
	then \
		echo 2>&1 "Use 'make sparse' instead"; \
		$(MAKE) --no-print-directory sparse; \
	else \
		echo 2>&1 "Did you mean 'make test'?"; \
		exit 1; \
	fi

### Installation rules

ifneq ($(filter /%,$(firstword $(template_dir))),)
template_instdir = $(template_dir)
else
template_instdir = $(prefix)/$(template_dir)
endif
export template_instdir

ifneq ($(filter /%,$(firstword $(gitexecdir))),)
gitexec_instdir = $(gitexecdir)
else
gitexec_instdir = $(prefix)/$(gitexecdir)
endif
gitexec_instdir_SQ = $(subst ','\'',$(gitexec_instdir))
export gitexec_instdir

ifneq ($(filter /%,$(firstword $(mergetoolsdir))),)
mergetools_instdir = $(mergetoolsdir)
else
mergetools_instdir = $(prefix)/$(mergetoolsdir)
endif
mergetools_instdir_SQ = $(subst ','\'',$(mergetools_instdir))

install_bindir_programs := $(patsubst %,%$X,$(BINDIR_PROGRAMS_NEED_X)) $(BINDIR_PROGRAMS_NO_X)

profile-install: profile
	$(MAKE) install

profile-fast-install: profile-fast
	$(MAKE) install

install: all
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(bindir_SQ)'
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(gitexec_instdir_SQ)'
	$(INSTALL) $(ALL_PROGRAMS) '$(DESTDIR_SQ)$(gitexec_instdir_SQ)'
	$(INSTALL) -m 644 $(SCRIPT_LIB) '$(DESTDIR_SQ)$(gitexec_instdir_SQ)'
	$(INSTALL) $(install_bindir_programs) '$(DESTDIR_SQ)$(bindir_SQ)'
	$(MAKE) -C templates DESTDIR='$(DESTDIR_SQ)' install
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(mergetools_instdir_SQ)'
	$(INSTALL) -m 644 mergetools/* '$(DESTDIR_SQ)$(mergetools_instdir_SQ)'
ifndef NO_GETTEXT
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(localedir_SQ)'
	(cd po/build/locale && $(TAR) cf - .) | \
	(cd '$(DESTDIR_SQ)$(localedir_SQ)' && umask 022 && $(TAR) xof -)
endif
ifndef NO_PERL
	$(MAKE) -C perl prefix='$(prefix_SQ)' DESTDIR='$(DESTDIR_SQ)' install
	$(MAKE) -C gitweb install
endif
ifndef NO_TCLTK
	$(MAKE) -C gitk-git install
	$(MAKE) -C git-gui gitexecdir='$(gitexec_instdir_SQ)' install
endif
ifneq (,$X)
	$(foreach p,$(patsubst %$X,%,$(filter %$X,$(ALL_PROGRAMS) $(BUILT_INS) git$X)), test '$(DESTDIR_SQ)$(gitexec_instdir_SQ)/$p' -ef '$(DESTDIR_SQ)$(gitexec_instdir_SQ)/$p$X' || $(RM) '$(DESTDIR_SQ)$(gitexec_instdir_SQ)/$p';)
endif

	bindir=$$(cd '$(DESTDIR_SQ)$(bindir_SQ)' && pwd) && \
	execdir=$$(cd '$(DESTDIR_SQ)$(gitexec_instdir_SQ)' && pwd) && \
	{ test "$$bindir/" = "$$execdir/" || \
	  for p in git$X $(filter $(install_bindir_programs),$(ALL_PROGRAMS)); do \
		$(RM) "$$execdir/$$p" && \
		test -z "$(NO_INSTALL_HARDLINKS)$(NO_CROSS_DIRECTORY_HARDLINKS)" && \
		ln "$$bindir/$$p" "$$execdir/$$p" 2>/dev/null || \
		cp "$$bindir/$$p" "$$execdir/$$p" || exit; \
	  done; \
	} && \
	for p in $(filter $(install_bindir_programs),$(BUILT_INS)); do \
		$(RM) "$$bindir/$$p" && \
		test -z "$(NO_INSTALL_HARDLINKS)" && \
		ln "$$bindir/git$X" "$$bindir/$$p" 2>/dev/null || \
		ln -s "git$X" "$$bindir/$$p" 2>/dev/null || \
		cp "$$bindir/git$X" "$$bindir/$$p" || exit; \
	done && \
	for p in $(BUILT_INS); do \
		$(RM) "$$execdir/$$p" && \
		test -z "$(NO_INSTALL_HARDLINKS)" && \
		ln "$$execdir/git$X" "$$execdir/$$p" 2>/dev/null || \
		ln -s "git$X" "$$execdir/$$p" 2>/dev/null || \
		cp "$$execdir/git$X" "$$execdir/$$p" || exit; \
	done && \
	remote_curl_aliases="$(REMOTE_CURL_ALIASES)" && \
	for p in $$remote_curl_aliases; do \
		$(RM) "$$execdir/$$p" && \
		test -z "$(NO_INSTALL_HARDLINKS)" && \
		ln "$$execdir/git-remote-http$X" "$$execdir/$$p" 2>/dev/null || \
		ln -s "git-remote-http$X" "$$execdir/$$p" 2>/dev/null || \
		cp "$$execdir/git-remote-http$X" "$$execdir/$$p" || exit; \
	done && \
	./check_bindir "z$$bindir" "z$$execdir" "$$bindir/git-add$X"

install-gitweb:
	$(MAKE) -C gitweb install

install-doc:
	$(MAKE) -C Documentation install

install-man:
	$(MAKE) -C Documentation install-man

install-html:
	$(MAKE) -C Documentation install-html

install-info:
	$(MAKE) -C Documentation install-info

install-pdf:
	$(MAKE) -C Documentation install-pdf

quick-install-doc:
	$(MAKE) -C Documentation quick-install

quick-install-man:
	$(MAKE) -C Documentation quick-install-man

quick-install-html:
	$(MAKE) -C Documentation quick-install-html



### Maintainer's dist rules

git.spec: git.spec.in GIT-VERSION-FILE
	sed -e 's/@@VERSION@@/$(GIT_VERSION)/g' < $< > $@+
	mv $@+ $@

GIT_TARNAME = git-$(GIT_VERSION)
dist: git.spec git-archive$(X) configure
	./git-archive --format=tar \
		--prefix=$(GIT_TARNAME)/ HEAD^{tree} > $(GIT_TARNAME).tar
	@mkdir -p $(GIT_TARNAME)
	@cp git.spec configure $(GIT_TARNAME)
	@echo $(GIT_VERSION) > $(GIT_TARNAME)/version
	@$(MAKE) -C git-gui TARDIR=../$(GIT_TARNAME)/git-gui dist-version
	$(TAR) rf $(GIT_TARNAME).tar \
		$(GIT_TARNAME)/git.spec \
		$(GIT_TARNAME)/configure \
		$(GIT_TARNAME)/version \
		$(GIT_TARNAME)/git-gui/version
	@$(RM) -r $(GIT_TARNAME)
	gzip -f -9 $(GIT_TARNAME).tar

rpm: dist
	$(RPMBUILD) \
		--define "_source_filedigest_algorithm md5" \
		--define "_binary_filedigest_algorithm md5" \
		-ta $(GIT_TARNAME).tar.gz

htmldocs = git-htmldocs-$(GIT_VERSION)
manpages = git-manpages-$(GIT_VERSION)
dist-doc:
	$(RM) -r .doc-tmp-dir
	mkdir .doc-tmp-dir
	$(MAKE) -C Documentation WEBDOC_DEST=../.doc-tmp-dir install-webdoc
	cd .doc-tmp-dir && $(TAR) cf ../$(htmldocs).tar .
	gzip -n -9 -f $(htmldocs).tar
	:
	$(RM) -r .doc-tmp-dir
	mkdir -p .doc-tmp-dir/man1 .doc-tmp-dir/man5 .doc-tmp-dir/man7
	$(MAKE) -C Documentation DESTDIR=./ \
		man1dir=../.doc-tmp-dir/man1 \
		man5dir=../.doc-tmp-dir/man5 \
		man7dir=../.doc-tmp-dir/man7 \
		install
	cd .doc-tmp-dir && $(TAR) cf ../$(manpages).tar .
	gzip -n -9 -f $(manpages).tar
	$(RM) -r .doc-tmp-dir

### Cleaning rules

distclean: clean
	$(RM) configure
	$(RM) config.log config.status config.cache
	$(RM) config.mak.autogen config.mak.append
	$(RM) -r autom4te.cache

profile-clean:
	$(RM) $(addsuffix *.gcda,$(addprefix $(PROFILE_DIR)/, $(object_dirs)))
	$(RM) $(addsuffix *.gcno,$(addprefix $(PROFILE_DIR)/, $(object_dirs)))

clean: profile-clean coverage-clean
	$(RM) *.o *.res block-sha1/*.o ppc/*.o compat/*.o compat/*/*.o
	$(RM) xdiff/*.o vcs-svn/*.o ewah/*.o builtin/*.o
	$(RM) $(LIB_FILE) $(XDIFF_LIB) $(VCSSVN_LIB)
	$(RM) $(ALL_PROGRAMS) $(SCRIPT_LIB) $(BUILT_INS) git$X
	$(RM) $(TEST_PROGRAMS) $(NO_INSTALL)
	$(RM) -r bin-wrappers $(dep_dirs)
	$(RM) -r po/build/
	$(RM) *.spec *.pyc *.pyo */*.pyc */*.pyo common-cmds.h $(ETAGS_TARGET) tags cscope*
	$(RM) -r $(GIT_TARNAME) .doc-tmp-dir
	$(RM) $(GIT_TARNAME).tar.gz git-core_$(GIT_VERSION)-*.tar.gz
	$(RM) $(htmldocs).tar.gz $(manpages).tar.gz
	$(MAKE) -C Documentation/ clean
ifndef NO_PERL
	$(MAKE) -C gitweb clean
	$(MAKE) -C perl clean
endif
	$(MAKE) -C templates/ clean
	$(MAKE) -C t/ clean
ifndef NO_TCLTK
	$(MAKE) -C gitk-git clean
	$(MAKE) -C git-gui clean
endif
	$(RM) GIT-VERSION-FILE GIT-CFLAGS GIT-LDFLAGS GIT-BUILD-OPTIONS
	$(RM) GIT-USER-AGENT GIT-PREFIX
	$(RM) GIT-SCRIPT-DEFINES GIT-PERL-DEFINES GIT-PYTHON-VARS

.PHONY: all install profile-clean clean strip
.PHONY: shell_compatibility_test please_set_SHELL_PATH_to_a_more_modern_shell
.PHONY: FORCE cscope

### Check documentation
#
ALL_COMMANDS = $(ALL_PROGRAMS) $(SCRIPT_LIB) $(BUILT_INS)
ALL_COMMANDS += git
ALL_COMMANDS += gitk
ALL_COMMANDS += gitweb
ALL_COMMANDS += git-gui git-citool
check-docs::
	@(for v in $(ALL_COMMANDS); \
	do \
		case "$$v" in \
		git-merge-octopus | git-merge-ours | git-merge-recursive | \
		git-merge-resolve | git-merge-subtree | \
		git-fsck-objects | git-init-db | \
		git-remote-* | git-stage | \
		git-?*--?* ) continue ;; \
		esac ; \
		test -f "Documentation/$$v.txt" || \
		echo "no doc: $$v"; \
		sed -e '/^#/d' command-list.txt | \
		grep -q "^$$v[ 	]" || \
		case "$$v" in \
		git) ;; \
		*) echo "no link: $$v";; \
		esac ; \
	done; \
	( \
		sed -e '/^#/d' \
		    -e 's/[ 	].*//' \
		    -e 's/^/listed /' command-list.txt; \
		$(MAKE) -C Documentation print-man1 | \
		grep '\.txt$$' | \
		sed -e 's|Documentation/|documented |' \
		    -e 's/\.txt//'; \
	) | while read how cmd; \
	do \
		case " $(ALL_COMMANDS) " in \
		*" $$cmd "*)	;; \
		*) echo "removed but $$how: $$cmd" ;; \
		esac; \
	done ) | sort

### Make sure built-ins do not have dups and listed in git.c
#
check-builtins::
	./check-builtins.sh

### Test suite coverage testing
#
.PHONY: coverage coverage-clean coverage-compile coverage-test coverage-report
.PHONY: coverage-clean-results

coverage:
	$(MAKE) coverage-test
	$(MAKE) coverage-untested-functions

object_dirs := $(sort $(dir $(OBJECTS)))
coverage-clean-results:
	$(RM) $(addsuffix *.gcov,$(object_dirs))
	$(RM) $(addsuffix *.gcda,$(object_dirs))
	$(RM) coverage-untested-functions
	$(RM) -r cover_db/
	$(RM) -r cover_db_html/

coverage-clean: coverage-clean-results
	$(RM) $(addsuffix *.gcno,$(object_dirs))

COVERAGE_CFLAGS = $(CFLAGS) -O0 -ftest-coverage -fprofile-arcs
COVERAGE_LDFLAGS = $(CFLAGS)  -O0 -lgcov
GCOVFLAGS = --preserve-paths --branch-probabilities --all-blocks

coverage-compile:
	$(MAKE) CFLAGS="$(COVERAGE_CFLAGS)" LDFLAGS="$(COVERAGE_LDFLAGS)" all

coverage-test: coverage-clean-results coverage-compile
	$(MAKE) CFLAGS="$(COVERAGE_CFLAGS)" LDFLAGS="$(COVERAGE_LDFLAGS)" \
		DEFAULT_TEST_TARGET=test -j1 test

coverage-report:
	$(QUIET_GCOV)for dir in $(object_dirs); do \
		$(GCOV) $(GCOVFLAGS) --object-directory=$$dir $$dir*.c || exit; \
	done

coverage-untested-functions: coverage-report
	grep '^function.*called 0 ' *.c.gcov \
		| sed -e 's/\([^:]*\)\.gcov: *function \([^ ]*\) called.*/\1: \2/' \
		> coverage-untested-functions

cover_db: coverage-report
	gcov2perl -db cover_db *.gcov

cover_db_html: cover_db
	cover -report html -outputdir cover_db_html cover_db

