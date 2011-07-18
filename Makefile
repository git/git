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
# Define NO_CURL if you do not have libcurl installed.  git-http-pull and
# git-http-push are not built, and you cannot use http:// and https://
# transports.
#
# Define CURLDIR=/foo/bar if your curl header and library files are in
# /foo/bar/include and /foo/bar/lib directories.
#
# Define NO_EXPAT if you do not have expat installed.  git-http-push is
# not built, and you cannot push using http:// and https:// transports.
#
# Define EXPATDIR=/foo/bar if your expat header and library files are in
# /foo/bar/include and /foo/bar/lib directories.
#
# Define HAVE_PATHS_H if you have paths.h and want to use the default PATH
# it specifies.
#
# Define NO_D_INO_IN_DIRENT if you don't have d_ino in your struct dirent.
#
# Define NO_D_TYPE_IN_DIRENT if your platform defines DT_UNKNOWN but lacks
# d_type in struct dirent (Cygwin 1.5, fixed in Cygwin 1.7).
#
# Define NO_STRCASESTR if you don't have strcasestr.
#
# Define NO_MEMMEM if you don't have memmem.
#
# Define NO_STRLCPY if you don't have strlcpy.
#
# Define NO_STRTOUMAX if you don't have strtoumax in the C library.
# If your compiler also does not support long long or does not have
# strtoull, define NO_STRTOULL.
#
# Define NO_SETENV if you don't have setenv in the C library.
#
# Define NO_UNSETENV if you don't have unsetenv in the C library.
#
# Define NO_MKDTEMP if you don't have mkdtemp in the C library.
#
# Define NO_MKSTEMPS if you don't have mkstemps in the C library.
#
# Define NO_STRTOK_R if you don't have strtok_r in the C library.
#
# Define NO_FNMATCH if you don't have fnmatch in the C library.
#
# Define NO_FNMATCH_CASEFOLD if your fnmatch function doesn't have the
# FNM_CASEFOLD GNU extension.
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
# Define BLK_SHA1 environment variable if you want the C version
# of the SHA1 that assumes you can do unaligned 32-bit loads and
# have a fast htonl() function.
#
# Define PPC_SHA1 environment variable when running make to make use of
# a bundled SHA1 routine optimized for PowerPC.
#
# Define NEEDS_CRYPTO_WITH_SSL if you need -lcrypto when using -lssl (Darwin).
#
# Define NEEDS_SSL_WITH_CRYPTO if you need -lssl when using -lcrypto (Darwin).
#
# Define NEEDS_LIBICONV if linking with libc is not enough (Darwin).
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
# Define NO_PTHREADS if you do not have or do not want to use Pthreads.
#
# Define NO_PREAD if you have a problem with pread() system call (e.g.
# cygwin1.dll before v1.5.22).
#
# Define NO_FAST_WORKING_DIRECTORY if accessing objects in pack files is
# generally faster on your platform than accessing the working directory.
#
# Define NO_TRUSTABLE_FILEMODE if your filesystem may claim to support
# the executable mode bit, but doesn't really do so.
#
# Define NO_IPV6 if you lack IPv6 support and getaddrinfo().
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
# Define ASCIIDOC7 if you want to format documentation with AsciiDoc 7
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
# Define NO_PERL_MAKEMAKER if you cannot use Makefiles generated by perl's
# MakeMaker (e.g. using ActiveState under Cygwin).
#
# Define NO_PERL if you do not want Perl scripts or libraries at all.
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
# Define USE_NED_ALLOCATOR if you want to replace the platforms default
# memory allocators with the nedmalloc allocator written by Niall Douglas.
#
# Define NO_REGEX if you have no or inferior regex support in your C library.
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
# Define COMPUTE_HEADER_DEPENDENCIES if your compiler supports the -MMD option
# and you want to avoid rebuilding objects when an unrelated header file
# changes.
#
# Define CHECK_HEADER_DEPENDENCIES to check for problems in the hard-coded
# dependency rules.
#
# Define NATIVE_CRLF if your platform uses CRLF for line endings.

GIT-VERSION-FILE: FORCE
	@$(SHELL_PATH) ./GIT-VERSION-GEN
-include GIT-VERSION-FILE

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
uname_M := $(shell sh -c 'uname -m 2>/dev/null || echo not')
uname_O := $(shell sh -c 'uname -o 2>/dev/null || echo not')
uname_R := $(shell sh -c 'uname -r 2>/dev/null || echo not')
uname_P := $(shell sh -c 'uname -p 2>/dev/null || echo not')
uname_V := $(shell sh -c 'uname -v 2>/dev/null || echo not')

ifdef MSVC
	# avoid the MingW and Cygwin configuration sections
	uname_S := Windows
	uname_O := Windows
endif

# CFLAGS and LDFLAGS are for the users to override from the command line.

CFLAGS = -g -O2 -Wall
LDFLAGS =
ALL_CFLAGS = $(CPPFLAGS) $(CFLAGS)
ALL_LDFLAGS = $(LDFLAGS)
STRIP ?= strip

# Among the variables below, these:
#   gitexecdir
#   template_dir
#   mandir
#   infodir
#   htmldir
#   sysconfdir
# can be specified as a relative path some/where/else;
# this is interpreted as relative to $(prefix) and "git" at
# runtime figures out where they are based on the path to the executable.
# This can help installing the suite in a relocatable way.

prefix = $(HOME)
bindir_relative = bin
bindir = $(prefix)/$(bindir_relative)
mandir = share/man
infodir = share/info
gitexecdir = libexec/git-core
sharedir = $(prefix)/share
gitwebdir = $(sharedir)/gitweb
template_dir = share/git-core/templates
htmldir = share/doc/git-doc
ETC_GITCONFIG = $(sysconfdir)/gitconfig
ETC_GITATTRIBUTES = $(sysconfdir)/gitattributes
lib = lib
# DESTDIR=
pathsep = :

export prefix bindir sharedir sysconfdir gitwebdir

CC = gcc
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
SCRIPT_SH += git-lost-found.sh
SCRIPT_SH += git-merge-octopus.sh
SCRIPT_SH += git-merge-one-file.sh
SCRIPT_SH += git-merge-resolve.sh
SCRIPT_SH += git-mergetool.sh
SCRIPT_SH += git-pull.sh
SCRIPT_SH += git-quiltimport.sh
SCRIPT_SH += git-rebase.sh
SCRIPT_SH += git-repack.sh
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

SCRIPT_PYTHON += git-remote-testgit.py

SCRIPTS = $(patsubst %.sh,%,$(SCRIPT_SH)) \
	  $(patsubst %.perl,%,$(SCRIPT_PERL)) \
	  $(patsubst %.py,%,$(SCRIPT_PYTHON)) \
	  git-instaweb

ETAGS_TARGET = TAGS

# Empty...
EXTRA_PROGRAMS =

# ... and all the rest that could be moved out of bindir to gitexecdir
PROGRAMS += $(EXTRA_PROGRAMS)

PROGRAM_OBJS += daemon.o
PROGRAM_OBJS += fast-import.o
PROGRAM_OBJS += imap-send.o
PROGRAM_OBJS += shell.o
PROGRAM_OBJS += show-index.o
PROGRAM_OBJS += upload-pack.o
PROGRAM_OBJS += http-backend.o
PROGRAM_OBJS += sh-i18n--envsubst.o

PROGRAMS += $(patsubst %.o,git-%$X,$(PROGRAM_OBJS))

TEST_PROGRAMS_NEED_X += test-chmtime
TEST_PROGRAMS_NEED_X += test-credential
TEST_PROGRAMS_NEED_X += test-ctype
TEST_PROGRAMS_NEED_X += test-date
TEST_PROGRAMS_NEED_X += test-delta
TEST_PROGRAMS_NEED_X += test-dump-cache-tree
TEST_PROGRAMS_NEED_X += test-genrandom
TEST_PROGRAMS_NEED_X += test-index-version
TEST_PROGRAMS_NEED_X += test-line-buffer
TEST_PROGRAMS_NEED_X += test-match-trees
TEST_PROGRAMS_NEED_X += test-mktemp
TEST_PROGRAMS_NEED_X += test-obj-pool
TEST_PROGRAMS_NEED_X += test-parse-options
TEST_PROGRAMS_NEED_X += test-path-utils
TEST_PROGRAMS_NEED_X += test-run-command
TEST_PROGRAMS_NEED_X += test-sha1
TEST_PROGRAMS_NEED_X += test-sigchain
TEST_PROGRAMS_NEED_X += test-string-pool
TEST_PROGRAMS_NEED_X += test-subprocess
TEST_PROGRAMS_NEED_X += test-svn-fe
TEST_PROGRAMS_NEED_X += test-treap

TEST_PROGRAMS = $(patsubst %,%$X,$(TEST_PROGRAMS_NEED_X))

# List built-in command $C whose implementation cmd_$C() is not in
# builtin/$C.o but is linked in as part of some other command.
BUILT_INS += $(patsubst builtin/%.o,git-%$X,$(BUILTIN_OBJS))

BUILT_INS += git-cherry$X
BUILT_INS += git-cherry-pick$X
BUILT_INS += git-format-patch$X
BUILT_INS += git-fsck-objects$X
BUILT_INS += git-get-tar-commit-id$X
BUILT_INS += git-init$X
BUILT_INS += git-merge-subtree$X
BUILT_INS += git-peek-remote$X
BUILT_INS += git-repo-config$X
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

LIB_FILE=libgit.a
XDIFF_LIB=xdiff/lib.a
VCSSVN_LIB=vcs-svn/lib.a

LIB_H += advice.h
LIB_H += archive.h
LIB_H += attr.h
LIB_H += blob.h
LIB_H += builtin.h
LIB_H += cache.h
LIB_H += cache-tree.h
LIB_H += color.h
LIB_H += commit.h
LIB_H += compat/bswap.h
LIB_H += compat/cygwin.h
LIB_H += compat/mingw.h
LIB_H += compat/win32/pthread.h
LIB_H += compat/win32/syslog.h
LIB_H += compat/win32/sys/poll.h
LIB_H += compat/win32/dirent.h
LIB_H += credential.h
LIB_H += csum-file.h
LIB_H += decorate.h
LIB_H += delta.h
LIB_H += diffcore.h
LIB_H += diff.h
LIB_H += dir.h
LIB_H += exec_cmd.h
LIB_H += fsck.h
LIB_H += gettext.h
LIB_H += git-compat-util.h
LIB_H += graph.h
LIB_H += grep.h
LIB_H += hash.h
LIB_H += help.h
LIB_H += levenshtein.h
LIB_H += list-objects.h
LIB_H += ll-merge.h
LIB_H += log-tree.h
LIB_H += mailmap.h
LIB_H += merge-file.h
LIB_H += merge-recursive.h
LIB_H += notes.h
LIB_H += notes-cache.h
LIB_H += notes-merge.h
LIB_H += object.h
LIB_H += pack.h
LIB_H += pack-refs.h
LIB_H += pack-revindex.h
LIB_H += parse-options.h
LIB_H += patch-ids.h
LIB_H += pkt-line.h
LIB_H += progress.h
LIB_H += quote.h
LIB_H += reflog-walk.h
LIB_H += refs.h
LIB_H += remote.h
LIB_H += rerere.h
LIB_H += resolve-undo.h
LIB_H += revision.h
LIB_H += run-command.h
LIB_H += sha1-array.h
LIB_H += sha1-lookup.h
LIB_H += sideband.h
LIB_H += sigchain.h
LIB_H += strbuf.h
LIB_H += streaming.h
LIB_H += string-list.h
LIB_H += submodule.h
LIB_H += tag.h
LIB_H += transport.h
LIB_H += tree.h
LIB_H += tree-walk.h
LIB_H += unpack-trees.h
LIB_H += userdiff.h
LIB_H += utf8.h
LIB_H += xdiff-interface.h
LIB_H += xdiff/xdiff.h

LIB_OBJS += abspath.o
LIB_OBJS += advice.o
LIB_OBJS += alias.o
LIB_OBJS += alloc.o
LIB_OBJS += archive.o
LIB_OBJS += archive-tar.o
LIB_OBJS += archive-zip.o
LIB_OBJS += attr.o
LIB_OBJS += base85.o
LIB_OBJS += bisect.o
LIB_OBJS += blob.o
LIB_OBJS += branch.o
LIB_OBJS += bundle.o
LIB_OBJS += cache-tree.o
LIB_OBJS += color.o
LIB_OBJS += combine-diff.o
LIB_OBJS += commit.o
LIB_OBJS += config.o
LIB_OBJS += connect.o
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
LIB_OBJS += exec_cmd.o
LIB_OBJS += fsck.o
LIB_OBJS += graph.o
LIB_OBJS += grep.o
LIB_OBJS += hash.o
LIB_OBJS += help.o
LIB_OBJS += hex.o
LIB_OBJS += ident.o
LIB_OBJS += levenshtein.o
LIB_OBJS += list-objects.o
LIB_OBJS += ll-merge.o
LIB_OBJS += lockfile.o
LIB_OBJS += log-tree.o
LIB_OBJS += mailmap.o
LIB_OBJS += match-trees.o
LIB_OBJS += merge-file.o
LIB_OBJS += merge-recursive.o
LIB_OBJS += name-hash.o
LIB_OBJS += notes.o
LIB_OBJS += notes-cache.o
LIB_OBJS += notes-merge.o
LIB_OBJS += object.o
LIB_OBJS += pack-check.o
LIB_OBJS += pack-refs.o
LIB_OBJS += pack-revindex.o
LIB_OBJS += pack-write.o
LIB_OBJS += pager.o
LIB_OBJS += parse-options.o
LIB_OBJS += patch-delta.o
LIB_OBJS += patch-ids.o
LIB_OBJS += path.o
LIB_OBJS += pkt-line.o
LIB_OBJS += preload-index.o
LIB_OBJS += pretty.o
LIB_OBJS += progress.o
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
LIB_OBJS += server-info.o
LIB_OBJS += setup.o
LIB_OBJS += sha1-array.o
LIB_OBJS += sha1-lookup.o
LIB_OBJS += sha1_file.o
LIB_OBJS += sha1_name.o
LIB_OBJS += shallow.o
LIB_OBJS += sideband.o
LIB_OBJS += sigchain.o
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
LIB_OBJS += usage.o
LIB_OBJS += userdiff.o
LIB_OBJS += utf8.o
LIB_OBJS += walker.o
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
BUILTIN_OBJS += builtin/check-ref-format.o
BUILTIN_OBJS += builtin/checkout-index.o
BUILTIN_OBJS += builtin/checkout.o
BUILTIN_OBJS += builtin/clean.o
BUILTIN_OBJS += builtin/clone.o
BUILTIN_OBJS += builtin/commit-tree.o
BUILTIN_OBJS += builtin/commit.o
BUILTIN_OBJS += builtin/config.o
BUILTIN_OBJS += builtin/count-objects.o
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
BUILTIN_OBJS += builtin/tar-tree.o
BUILTIN_OBJS += builtin/unpack-file.o
BUILTIN_OBJS += builtin/unpack-objects.o
BUILTIN_OBJS += builtin/update-index.o
BUILTIN_OBJS += builtin/update-ref.o
BUILTIN_OBJS += builtin/update-server-info.o
BUILTIN_OBJS += builtin/upload-archive.o
BUILTIN_OBJS += builtin/var.o
BUILTIN_OBJS += builtin/verify-pack.o
BUILTIN_OBJS += builtin/verify-tag.o
BUILTIN_OBJS += builtin/write-tree.o

GITLIBS = $(LIB_FILE) $(XDIFF_LIB)
EXTLIBS =

#
# Platform specific tweaks
#

# We choose to avoid "if .. else if .. else .. endif endif"
# because maintaining the nesting to match is a pain.  If
# we had "elif" things would have been much nicer...

ifeq ($(uname_S),OSF1)
	# Need this for u_short definitions et al
	BASIC_CFLAGS += -D_OSF_SOURCE
	SOCKLEN_T = int
	NO_STRTOULL = YesPlease
	NO_NSEC = YesPlease
endif
ifeq ($(uname_S),Linux)
	NO_STRLCPY = YesPlease
	NO_MKSTEMPS = YesPlease
	HAVE_PATHS_H = YesPlease
endif
ifeq ($(uname_S),GNU/kFreeBSD)
	NO_STRLCPY = YesPlease
	NO_MKSTEMPS = YesPlease
	HAVE_PATHS_H = YesPlease
endif
ifeq ($(uname_S),UnixWare)
	CC = cc
	NEEDS_SOCKET = YesPlease
	NEEDS_NSL = YesPlease
	NEEDS_SSL_WITH_CRYPTO = YesPlease
	NEEDS_LIBICONV = YesPlease
	SHELL_PATH = /usr/local/bin/bash
	NO_IPV6 = YesPlease
	NO_HSTRERROR = YesPlease
	NO_MKSTEMPS = YesPlease
	BASIC_CFLAGS += -Kthread
	BASIC_CFLAGS += -I/usr/local/include
	BASIC_LDFLAGS += -L/usr/local/lib
	INSTALL = ginstall
	TAR = gtar
	NO_STRCASESTR = YesPlease
	NO_MEMMEM = YesPlease
endif
ifeq ($(uname_S),SCO_SV)
	ifeq ($(uname_R),3.2)
		CFLAGS = -O2
	endif
	ifeq ($(uname_R),5)
		CC = cc
		BASIC_CFLAGS += -Kthread
	endif
	NEEDS_SOCKET = YesPlease
	NEEDS_NSL = YesPlease
	NEEDS_SSL_WITH_CRYPTO = YesPlease
	NEEDS_LIBICONV = YesPlease
	SHELL_PATH = /usr/bin/bash
	NO_IPV6 = YesPlease
	NO_HSTRERROR = YesPlease
	NO_MKSTEMPS = YesPlease
	BASIC_CFLAGS += -I/usr/local/include
	BASIC_LDFLAGS += -L/usr/local/lib
	NO_STRCASESTR = YesPlease
	NO_MEMMEM = YesPlease
	INSTALL = ginstall
	TAR = gtar
endif
ifeq ($(uname_S),Darwin)
	NEEDS_CRYPTO_WITH_SSL = YesPlease
	NEEDS_SSL_WITH_CRYPTO = YesPlease
	NEEDS_LIBICONV = YesPlease
	ifeq ($(shell expr "$(uname_R)" : '[15678]\.'),2)
		OLD_ICONV = UnfortunatelyYes
	endif
	ifeq ($(shell expr "$(uname_R)" : '[15]\.'),2)
		NO_STRLCPY = YesPlease
	endif
	NO_MEMMEM = YesPlease
	USE_ST_TIMESPEC = YesPlease
endif
ifeq ($(uname_S),SunOS)
	NEEDS_SOCKET = YesPlease
	NEEDS_NSL = YesPlease
	SHELL_PATH = /bin/bash
	SANE_TOOL_PATH = /usr/xpg6/bin:/usr/xpg4/bin
	NO_STRCASESTR = YesPlease
	NO_MEMMEM = YesPlease
	NO_MKDTEMP = YesPlease
	NO_MKSTEMPS = YesPlease
	NO_REGEX = YesPlease
	NO_FNMATCH_CASEFOLD = YesPlease
	ifeq ($(uname_R),5.6)
		SOCKLEN_T = int
		NO_HSTRERROR = YesPlease
		NO_IPV6 = YesPlease
		NO_SOCKADDR_STORAGE = YesPlease
		NO_UNSETENV = YesPlease
		NO_SETENV = YesPlease
		NO_STRLCPY = YesPlease
		NO_STRTOUMAX = YesPlease
		GIT_TEST_CMP = cmp
	endif
	ifeq ($(uname_R),5.7)
		NEEDS_RESOLV = YesPlease
		NO_IPV6 = YesPlease
		NO_SOCKADDR_STORAGE = YesPlease
		NO_UNSETENV = YesPlease
		NO_SETENV = YesPlease
		NO_STRLCPY = YesPlease
		NO_STRTOUMAX = YesPlease
		GIT_TEST_CMP = cmp
	endif
	ifeq ($(uname_R),5.8)
		NO_UNSETENV = YesPlease
		NO_SETENV = YesPlease
		NO_STRTOUMAX = YesPlease
		GIT_TEST_CMP = cmp
	endif
	ifeq ($(uname_R),5.9)
		NO_UNSETENV = YesPlease
		NO_SETENV = YesPlease
		NO_STRTOUMAX = YesPlease
		GIT_TEST_CMP = cmp
	endif
	INSTALL = /usr/ucb/install
	TAR = gtar
	BASIC_CFLAGS += -D__EXTENSIONS__ -D__sun__ -DHAVE_ALLOCA_H
endif
ifeq ($(uname_O),Cygwin)
	ifeq ($(shell expr "$(uname_R)" : '1\.[1-6]\.'),4)
		NO_D_TYPE_IN_DIRENT = YesPlease
		NO_D_INO_IN_DIRENT = YesPlease
		NO_STRCASESTR = YesPlease
		NO_MEMMEM = YesPlease
		NO_MKSTEMPS = YesPlease
		NO_SYMLINK_HEAD = YesPlease
		NO_IPV6 = YesPlease
		OLD_ICONV = UnfortunatelyYes
	endif
	NEEDS_LIBICONV = YesPlease
	NO_FAST_WORKING_DIRECTORY = UnfortunatelyYes
	NO_TRUSTABLE_FILEMODE = UnfortunatelyYes
	NO_ST_BLOCKS_IN_STRUCT_STAT = YesPlease
	# There are conflicting reports about this.
	# On some boxes NO_MMAP is needed, and not so elsewhere.
	# Try commenting this out if you suspect MMAP is more efficient
	NO_MMAP = YesPlease
	X = .exe
	COMPAT_OBJS += compat/cygwin.o
	UNRELIABLE_FSTAT = UnfortunatelyYes
	SPARSE_FLAGS = -isystem /usr/include/w32api -Wno-one-bit-signed-bitfield
endif
ifeq ($(uname_S),FreeBSD)
	NEEDS_LIBICONV = YesPlease
	OLD_ICONV = YesPlease
	NO_MEMMEM = YesPlease
	BASIC_CFLAGS += -I/usr/local/include
	BASIC_LDFLAGS += -L/usr/local/lib
	DIR_HAS_BSD_GROUP_SEMANTICS = YesPlease
	USE_ST_TIMESPEC = YesPlease
	ifeq ($(shell expr "$(uname_R)" : '4\.'),2)
		PTHREAD_LIBS = -pthread
		NO_UINTMAX_T = YesPlease
		NO_STRTOUMAX = YesPlease
	endif
	PYTHON_PATH = /usr/local/bin/python
	HAVE_PATHS_H = YesPlease
endif
ifeq ($(uname_S),OpenBSD)
	NO_STRCASESTR = YesPlease
	NO_MEMMEM = YesPlease
	USE_ST_TIMESPEC = YesPlease
	NEEDS_LIBICONV = YesPlease
	BASIC_CFLAGS += -I/usr/local/include
	BASIC_LDFLAGS += -L/usr/local/lib
	HAVE_PATHS_H = YesPlease
endif
ifeq ($(uname_S),NetBSD)
	ifeq ($(shell expr "$(uname_R)" : '[01]\.'),2)
		NEEDS_LIBICONV = YesPlease
	endif
	BASIC_CFLAGS += -I/usr/pkg/include
	BASIC_LDFLAGS += -L/usr/pkg/lib $(CC_LD_DYNPATH)/usr/pkg/lib
	USE_ST_TIMESPEC = YesPlease
	NO_MKSTEMPS = YesPlease
	HAVE_PATHS_H = YesPlease
endif
ifeq ($(uname_S),AIX)
	DEFAULT_PAGER = more
	NO_STRCASESTR=YesPlease
	NO_MEMMEM = YesPlease
	NO_MKDTEMP = YesPlease
	NO_MKSTEMPS = YesPlease
	NO_STRLCPY = YesPlease
	NO_NSEC = YesPlease
	FREAD_READS_DIRECTORIES = UnfortunatelyYes
	INTERNAL_QSORT = UnfortunatelyYes
	NEEDS_LIBICONV=YesPlease
	BASIC_CFLAGS += -D_LARGE_FILES
	ifeq ($(shell expr "$(uname_V)" : '[1234]'),1)
		NO_PTHREADS = YesPlease
	else
		PTHREAD_LIBS = -lpthread
	endif
	ifeq ($(shell expr "$(uname_V).$(uname_R)" : '5\.1'),3)
		INLINE=''
	endif
	GIT_TEST_CMP = cmp
endif
ifeq ($(uname_S),GNU)
	# GNU/Hurd
	NO_STRLCPY=YesPlease
	NO_MKSTEMPS = YesPlease
	HAVE_PATHS_H = YesPlease
endif
ifeq ($(uname_S),IRIX)
	NO_SETENV = YesPlease
	NO_UNSETENV = YesPlease
	NO_STRCASESTR = YesPlease
	NO_MEMMEM = YesPlease
	NO_MKSTEMPS = YesPlease
	NO_MKDTEMP = YesPlease
	# When compiled with the MIPSpro 7.4.4m compiler, and without pthreads
	# (i.e. NO_PTHREADS is set), and _with_ MMAP (i.e. NO_MMAP is not set),
	# git dies with a segmentation fault when trying to access the first
	# entry of a reflog.  The conservative choice is made to always set
	# NO_MMAP.  If you suspect that your compiler is not affected by this
	# issue, comment out the NO_MMAP statement.
	NO_MMAP = YesPlease
	NO_REGEX = YesPlease
	NO_FNMATCH_CASEFOLD = YesPlease
	SNPRINTF_RETURNS_BOGUS = YesPlease
	SHELL_PATH = /usr/gnu/bin/bash
	NEEDS_LIBGEN = YesPlease
endif
ifeq ($(uname_S),IRIX64)
	NO_SETENV=YesPlease
	NO_UNSETENV = YesPlease
	NO_STRCASESTR=YesPlease
	NO_MEMMEM = YesPlease
	NO_MKSTEMPS = YesPlease
	NO_MKDTEMP = YesPlease
	# When compiled with the MIPSpro 7.4.4m compiler, and without pthreads
	# (i.e. NO_PTHREADS is set), and _with_ MMAP (i.e. NO_MMAP is not set),
	# git dies with a segmentation fault when trying to access the first
	# entry of a reflog.  The conservative choice is made to always set
	# NO_MMAP.  If you suspect that your compiler is not affected by this
	# issue, comment out the NO_MMAP statement.
	NO_MMAP = YesPlease
	NO_REGEX = YesPlease
	NO_FNMATCH_CASEFOLD = YesPlease
	SNPRINTF_RETURNS_BOGUS = YesPlease
	SHELL_PATH=/usr/gnu/bin/bash
	NEEDS_LIBGEN = YesPlease
endif
ifeq ($(uname_S),HP-UX)
	INLINE = __inline
	NO_IPV6=YesPlease
	NO_SETENV=YesPlease
	NO_STRCASESTR=YesPlease
	NO_MEMMEM = YesPlease
	NO_MKSTEMPS = YesPlease
	NO_STRLCPY = YesPlease
	NO_MKDTEMP = YesPlease
	NO_UNSETENV = YesPlease
	NO_HSTRERROR = YesPlease
	NO_SYS_SELECT_H = YesPlease
	NO_FNMATCH_CASEFOLD = YesPlease
	SNPRINTF_RETURNS_BOGUS = YesPlease
	NO_NSEC = YesPlease
	ifeq ($(uname_R),B.11.00)
		NO_INET_NTOP = YesPlease
		NO_INET_PTON = YesPlease
	endif
	ifeq ($(uname_R),B.10.20)
		# Override HP-UX 11.x setting:
		INLINE =
		SOCKLEN_T = size_t
		NO_PREAD = YesPlease
		NO_INET_NTOP = YesPlease
		NO_INET_PTON = YesPlease
	endif
	GIT_TEST_CMP = cmp
endif
ifeq ($(uname_S),Windows)
	GIT_VERSION := $(GIT_VERSION).MSVC
	pathsep = ;
	NO_PREAD = YesPlease
	NEEDS_CRYPTO_WITH_SSL = YesPlease
	NO_LIBGEN_H = YesPlease
	NO_SYMLINK_HEAD = YesPlease
	NO_IPV6 = YesPlease
	NO_SETENV = YesPlease
	NO_UNSETENV = YesPlease
	NO_STRCASESTR = YesPlease
	NO_STRLCPY = YesPlease
	NO_STRTOK_R = YesPlease
	NO_FNMATCH = YesPlease
	NO_MEMMEM = YesPlease
	# NEEDS_LIBICONV = YesPlease
	NO_ICONV = YesPlease
	NO_STRTOUMAX = YesPlease
	NO_STRTOULL = YesPlease
	NO_MKDTEMP = YesPlease
	NO_MKSTEMPS = YesPlease
	SNPRINTF_RETURNS_BOGUS = YesPlease
	NO_SVN_TESTS = YesPlease
	NO_PERL_MAKEMAKER = YesPlease
	RUNTIME_PREFIX = YesPlease
	NO_ST_BLOCKS_IN_STRUCT_STAT = YesPlease
	NO_NSEC = YesPlease
	USE_WIN32_MMAP = YesPlease
	# USE_NED_ALLOCATOR = YesPlease
	UNRELIABLE_FSTAT = UnfortunatelyYes
	OBJECT_CREATION_USES_RENAMES = UnfortunatelyNeedsTo
	NO_REGEX = YesPlease
	NO_CURL = YesPlease
	NO_PYTHON = YesPlease
	BLK_SHA1 = YesPlease
	NO_POSIX_GOODIES = UnfortunatelyYes
	NATIVE_CRLF = YesPlease

	CC = compat/vcbuild/scripts/clink.pl
	AR = compat/vcbuild/scripts/lib.pl
	CFLAGS =
	BASIC_CFLAGS = -nologo -I. -I../zlib -Icompat/vcbuild -Icompat/vcbuild/include -DWIN32 -D_CONSOLE -DHAVE_STRING_H -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE
	COMPAT_OBJS = compat/msvc.o compat/winansi.o \
		compat/win32/pthread.o compat/win32/syslog.o \
		compat/win32/sys/poll.o compat/win32/dirent.o
	COMPAT_CFLAGS = -D__USE_MINGW_ACCESS -DNOGDI -DHAVE_STRING_H -DHAVE_ALLOCA_H -Icompat -Icompat/regex -Icompat/win32 -DSTRIP_EXTENSION=\".exe\"
	BASIC_LDFLAGS = -IGNORE:4217 -IGNORE:4049 -NOLOGO -SUBSYSTEM:CONSOLE -NODEFAULTLIB:MSVCRT.lib
	EXTLIBS = user32.lib advapi32.lib shell32.lib wininet.lib ws2_32.lib
	PTHREAD_LIBS =
	lib =
ifndef DEBUG
	BASIC_CFLAGS += -GL -Os -MT
	BASIC_LDFLAGS += -LTCG
	AR += -LTCG
else
	BASIC_CFLAGS += -Zi -MTd
endif
	X = .exe
endif
ifeq ($(uname_S),Interix)
	NO_INITGROUPS = YesPlease
	NO_IPV6 = YesPlease
	NO_MEMMEM = YesPlease
	NO_MKDTEMP = YesPlease
	NO_STRTOUMAX = YesPlease
	NO_NSEC = YesPlease
	NO_MKSTEMPS = YesPlease
	ifeq ($(uname_R),3.5)
		NO_INET_NTOP = YesPlease
		NO_INET_PTON = YesPlease
		NO_SOCKADDR_STORAGE = YesPlease
		NO_FNMATCH_CASEFOLD = YesPlease
	endif
	ifeq ($(uname_R),5.2)
		NO_INET_NTOP = YesPlease
		NO_INET_PTON = YesPlease
		NO_SOCKADDR_STORAGE = YesPlease
		NO_FNMATCH_CASEFOLD = YesPlease
	endif
endif
ifneq (,$(findstring MINGW,$(uname_S)))
	pathsep = ;
	NO_PREAD = YesPlease
	NEEDS_CRYPTO_WITH_SSL = YesPlease
	NO_LIBGEN_H = YesPlease
	NO_SYMLINK_HEAD = YesPlease
	NO_SETENV = YesPlease
	NO_UNSETENV = YesPlease
	NO_STRCASESTR = YesPlease
	NO_STRLCPY = YesPlease
	NO_STRTOK_R = YesPlease
	NO_FNMATCH = YesPlease
	NO_MEMMEM = YesPlease
	NEEDS_LIBICONV = YesPlease
	OLD_ICONV = YesPlease
	NO_STRTOUMAX = YesPlease
	NO_MKDTEMP = YesPlease
	NO_MKSTEMPS = YesPlease
	NO_SVN_TESTS = YesPlease
	NO_PERL_MAKEMAKER = YesPlease
	RUNTIME_PREFIX = YesPlease
	NO_ST_BLOCKS_IN_STRUCT_STAT = YesPlease
	NO_NSEC = YesPlease
	USE_WIN32_MMAP = YesPlease
	USE_NED_ALLOCATOR = YesPlease
	UNRELIABLE_FSTAT = UnfortunatelyYes
	OBJECT_CREATION_USES_RENAMES = UnfortunatelyNeedsTo
	NO_REGEX = YesPlease
	NO_PYTHON = YesPlease
	BLK_SHA1 = YesPlease
	ETAGS_TARGET = ETAGS
	NO_INET_PTON = YesPlease
	NO_INET_NTOP = YesPlease
	NO_POSIX_GOODIES = UnfortunatelyYes
	COMPAT_CFLAGS += -D__USE_MINGW_ACCESS -DNOGDI -Icompat -Icompat/win32
	COMPAT_CFLAGS += -DSTRIP_EXTENSION=\".exe\"
	COMPAT_OBJS += compat/mingw.o compat/winansi.o \
		compat/win32/pthread.o compat/win32/syslog.o \
		compat/win32/sys/poll.o compat/win32/dirent.o
	EXTLIBS += -lws2_32
	PTHREAD_LIBS =
	X = .exe
	SPARSE_FLAGS = -Wno-one-bit-signed-bitfield
ifneq (,$(wildcard ../THIS_IS_MSYSGIT))
	htmldir=doc/git/html/
	prefix =
	INSTALL = /bin/install
	EXTLIBS += /mingw/lib/libz.a
	NO_R_TO_GCC_LINKER = YesPlease
	INTERNAL_QSORT = YesPlease
else
	NO_CURL = YesPlease
endif
endif

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
COMPUTE_HEADER_DEPENDENCIES =
USE_COMPUTED_HEADER_DEPENDENCIES =
endif

ifdef COMPUTE_HEADER_DEPENDENCIES
USE_COMPUTED_HEADER_DEPENDENCIES = YesPlease
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
	REMOTE_CURL_PRIMARY = git-remote-http$X
	REMOTE_CURL_ALIASES = git-remote-https$X git-remote-ftp$X git-remote-ftps$X
	REMOTE_CURL_NAMES = $(REMOTE_CURL_PRIMARY) $(REMOTE_CURL_ALIASES)
	PROGRAM_OBJS += http-fetch.o
	PROGRAMS += $(REMOTE_CURL_NAMES)
	curl_check := $(shell (echo 070908; curl-config --vernum) | sort -r | sed -ne 2p)
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
		OPENSSL_LINK += -lcrypto
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
endif
ifdef NEEDS_LIBICONV
	ifdef ICONVDIR
		BASIC_CFLAGS += -I$(ICONVDIR)/include
		ICONV_LINK = -L$(ICONVDIR)/$(lib) $(CC_LD_DYNPATH)$(ICONVDIR)/$(lib)
	else
		ICONV_LINK =
	endif
	EXTLIBS += $(ICONV_LINK) -liconv
endif
ifdef NEEDS_LIBGEN
	EXTLIBS += -lgen
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
	LIB_OBJS += gettext.o
	BASIC_CFLAGS += -DGETTEXT_POISON
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
	COMPAT_OBJS += compat/strtoumax.o
endif
ifdef NO_STRTOULL
	COMPAT_CFLAGS += -DNO_STRTOULL
endif
ifdef NO_STRTOK_R
	COMPAT_CFLAGS += -DNO_STRTOK_R
	COMPAT_OBJS += compat/strtok_r.o
endif
ifdef NO_FNMATCH
	COMPAT_CFLAGS += -Icompat/fnmatch
	COMPAT_CFLAGS += -DNO_FNMATCH
	COMPAT_OBJS += compat/fnmatch/fnmatch.o
else
ifdef NO_FNMATCH_CASEFOLD
	COMPAT_CFLAGS += -Icompat/fnmatch
	COMPAT_CFLAGS += -DNO_FNMATCH_CASEFOLD
	COMPAT_OBJS += compat/fnmatch/fnmatch.o
endif
endif
ifdef NO_SETENV
	COMPAT_CFLAGS += -DNO_SETENV
	COMPAT_OBJS += compat/setenv.o
endif
ifdef NO_MKDTEMP
	COMPAT_CFLAGS += -DNO_MKDTEMP
	COMPAT_OBJS += compat/mkdtemp.o
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
	SHA1_HEADER = <openssl/sha.h>
	EXTLIBS += $(LIB_4_CRYPTO)
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

ifeq ($(TCLTK_PATH),)
NO_TCLTK=NoThanks
endif

ifeq ($(PERL_PATH),)
NO_PERL=NoThanks
endif

ifeq ($(PYTHON_PATH),)
NO_PYTHON=NoThanks
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
	QUIET_GCOV     = @echo '   ' GCOV $@;
	QUIET_SP       = @echo '   ' SP $<;
	QUIET_SUBDIR0  = +@subdir=
	QUIET_SUBDIR1  = ;$(NO_SUBDIR) echo '   ' SUBDIR $$subdir; \
			 $(MAKE) $(PRINT_DIR) -C $$subdir
	export V
	export QUIET_GEN
	export QUIET_BUILT_IN
endif
endif

ifdef ASCIIDOC7
	export ASCIIDOC7
endif

# Shell quote (do not use $(call) to accommodate ancient setups);

SHA1_HEADER_SQ = $(subst ','\'',$(SHA1_HEADER))
ETC_GITCONFIG_SQ = $(subst ','\'',$(ETC_GITCONFIG))
ETC_GITATTRIBUTES_SQ = $(subst ','\'',$(ETC_GITATTRIBUTES))

DESTDIR_SQ = $(subst ','\'',$(DESTDIR))
bindir_SQ = $(subst ','\'',$(bindir))
bindir_relative_SQ = $(subst ','\'',$(bindir_relative))
mandir_SQ = $(subst ','\'',$(mandir))
infodir_SQ = $(subst ','\'',$(infodir))
gitexecdir_SQ = $(subst ','\'',$(gitexecdir))
template_dir_SQ = $(subst ','\'',$(template_dir))
htmldir_SQ = $(subst ','\'',$(htmldir))
prefix_SQ = $(subst ','\'',$(prefix))
gitwebdir_SQ = $(subst ','\'',$(gitwebdir))

SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))
PERL_PATH_SQ = $(subst ','\'',$(PERL_PATH))
PYTHON_PATH_SQ = $(subst ','\'',$(PYTHON_PATH))
TCLTK_PATH_SQ = $(subst ','\'',$(TCLTK_PATH))
DIFF_SQ = $(subst ','\'',$(DIFF))

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

ALL_CFLAGS += $(BASIC_CFLAGS)
ALL_LDFLAGS += $(BASIC_LDFLAGS)

export DIFF TAR INSTALL DESTDIR SHELL_PATH


### Build rules

SHELL = $(SHELL_PATH)

all:: shell_compatibility_test $(ALL_PROGRAMS) $(SCRIPT_LIB) $(BUILT_INS) $(OTHER_PROGRAMS) GIT-BUILD-OPTIONS
ifneq (,$X)
	$(QUIET_BUILT_IN)$(foreach p,$(patsubst %$X,%,$(filter %$X,$(ALL_PROGRAMS) $(BUILT_INS) git$X)), test -d '$p' -o '$p' -ef '$p$X' || $(RM) '$p';)
endif

all::
ifndef NO_TCLTK
	$(QUIET_SUBDIR0)git-gui $(QUIET_SUBDIR1) gitexecdir='$(gitexec_instdir_SQ)' all
	$(QUIET_SUBDIR0)gitk-git $(QUIET_SUBDIR1) all
endif
ifndef NO_PERL
	$(QUIET_SUBDIR0)perl $(QUIET_SUBDIR1) PERL_PATH='$(PERL_PATH_SQ)' prefix='$(prefix_SQ)' all
endif
ifndef NO_PYTHON
	$(QUIET_SUBDIR0)git_remote_helpers $(QUIET_SUBDIR1) PYTHON_PATH='$(PYTHON_PATH_SQ)' prefix='$(prefix_SQ)' all
endif
	$(QUIET_SUBDIR0)templates $(QUIET_SUBDIR1) SHELL_PATH='$(SHELL_PATH_SQ)' PERL_PATH='$(PERL_PATH_SQ)'

please_set_SHELL_PATH_to_a_more_modern_shell:
	@$$(:)

shell_compatibility_test: please_set_SHELL_PATH_to_a_more_modern_shell

strip: $(PROGRAMS) git$X
	$(STRIP) $(STRIP_OPTS) $(PROGRAMS) git$X

git.o: common-cmds.h
git.sp git.s git.o: EXTRA_CPPFLAGS = -DGIT_VERSION='"$(GIT_VERSION)"' \
	'-DGIT_HTML_PATH="$(htmldir_SQ)"' \
	'-DGIT_MAN_PATH="$(mandir_SQ)"' \
	'-DGIT_INFO_PATH="$(infodir_SQ)"'

git$X: git.o GIT-LDFLAGS $(BUILTIN_OBJS) $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ git.o \
		$(BUILTIN_OBJS) $(ALL_LDFLAGS) $(LIBS)

help.sp help.o: common-cmds.h

builtin/help.sp builtin/help.o: common-cmds.h
builtin/help.sp builtin/help.s builtin/help.o: EXTRA_CPPFLAGS = \
	'-DGIT_HTML_PATH="$(htmldir_SQ)"' \
	'-DGIT_MAN_PATH="$(mandir_SQ)"' \
	'-DGIT_INFO_PATH="$(infodir_SQ)"'

$(BUILT_INS): git$X
	$(QUIET_BUILT_IN)$(RM) $@ && \
	ln git$X $@ 2>/dev/null || \
	ln -s git$X $@ 2>/dev/null || \
	cp git$X $@

common-cmds.h: ./generate-cmdlist.sh command-list.txt

common-cmds.h: $(wildcard Documentation/git-*.txt)
	$(QUIET_GEN)./generate-cmdlist.sh > $@+ && mv $@+ $@

define cmd_munge_script
$(RM) $@ $@+ && \
sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
    -e 's|@SHELL_PATH@|$(SHELL_PATH_SQ)|' \
    -e 's|@@DIFF@@|$(DIFF_SQ)|' \
    -e 's/@@GIT_VERSION@@/$(GIT_VERSION)/g' \
    -e 's/@@NO_CURL@@/$(NO_CURL)/g' \
    -e $(BROKEN_PATH_FIX) \
    $@.sh >$@+
endef

$(patsubst %.sh,%,$(SCRIPT_SH)) : % : %.sh
	$(QUIET_GEN)$(cmd_munge_script) && \
	chmod +x $@+ && \
	mv $@+ $@

$(SCRIPT_LIB) : % : %.sh
	$(QUIET_GEN)$(cmd_munge_script) && \
	mv $@+ $@

ifndef NO_PERL
$(patsubst %.perl,%,$(SCRIPT_PERL)): perl/perl.mak

perl/perl.mak: GIT-CFLAGS perl/Makefile perl/Makefile.PL
	$(QUIET_SUBDIR0)perl $(QUIET_SUBDIR1) PERL_PATH='$(PERL_PATH_SQ)' prefix='$(prefix_SQ)' $(@F)

$(patsubst %.perl,%,$(SCRIPT_PERL)): % : %.perl
	$(QUIET_GEN)$(RM) $@ $@+ && \
	INSTLIBDIR=`MAKEFLAGS= $(MAKE) -C perl -s --no-print-directory instlibdir` && \
	sed -e '1{' \
	    -e '	s|#!.*perl|#!$(PERL_PATH_SQ)|' \
	    -e '	h' \
	    -e '	s=.*=use lib (split(/$(pathsep)/, $$ENV{GITPERLLIB} || "'"$$INSTLIBDIR"'"));=' \
	    -e '	H' \
	    -e '	x' \
	    -e '}' \
	    -e 's/@@GIT_VERSION@@/$(GIT_VERSION)/g' \
	    $@.perl >$@+ && \
	chmod +x $@+ && \
	mv $@+ $@


.PHONY: gitweb
gitweb:
	$(QUIET_SUBDIR0)gitweb $(QUIET_SUBDIR1) all

git-instaweb: git-instaweb.sh gitweb
	$(QUIET_GEN)$(RM) $@ $@+ && \
	sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
	    -e 's/@@GIT_VERSION@@/$(GIT_VERSION)/g' \
	    -e 's/@@NO_CURL@@/$(NO_CURL)/g' \
	    -e 's|@@GITWEBDIR@@|$(gitwebdir_SQ)|g' \
	    -e 's|@@PERL@@|$(PERL_PATH_SQ)|g' \
	    $@.sh > $@+ && \
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
$(patsubst %.py,%,$(SCRIPT_PYTHON)): GIT-CFLAGS
$(patsubst %.py,%,$(SCRIPT_PYTHON)): % : %.py
	$(QUIET_GEN)$(RM) $@ $@+ && \
	INSTLIBDIR=`MAKEFLAGS= $(MAKE) -C git_remote_helpers -s \
		--no-print-directory prefix='$(prefix_SQ)' DESTDIR='$(DESTDIR_SQ)' \
		instlibdir` && \
	sed -e '1s|#!.*python|#!$(PYTHON_PATH_SQ)|' \
	    -e 's|\(os\.getenv("GITPYTHONLIB"\)[^)]*)|\1,"@@INSTLIBDIR@@")|' \
	    -e 's|@@INSTLIBDIR@@|'"$$INSTLIBDIR"'|g' \
	    $@.py >$@+ && \
	chmod +x $@+ && \
	mv $@+ $@
else # NO_PYTHON
$(patsubst %.py,%,$(SCRIPT_PYTHON)): % : unimplemented.sh
	$(QUIET_GEN)$(RM) $@ $@+ && \
	sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
	    -e 's|@@REASON@@|NO_PYTHON=$(NO_PYTHON)|g' \
	    unimplemented.sh >$@+ && \
	chmod +x $@+ && \
	mv $@+ $@
endif # NO_PYTHON

configure: configure.ac
	$(QUIET_GEN)$(RM) $@ $<+ && \
	sed -e 's/@@GIT_VERSION@@/$(GIT_VERSION)/g' \
	    $< > $<+ && \
	autoconf -o $@ $<+ && \
	$(RM) $<+

# These can record GIT_VERSION
git.o git.spec \
	$(patsubst %.sh,%,$(SCRIPT_SH)) \
	$(patsubst %.perl,%,$(SCRIPT_PERL)) \
	: GIT-VERSION-FILE

TEST_OBJS := $(patsubst test-%$X,test-%.o,$(TEST_PROGRAMS))
GIT_OBJS := $(LIB_OBJS) $(BUILTIN_OBJS) $(PROGRAM_OBJS) $(TEST_OBJS) \
	git.o
ifndef NO_CURL
	GIT_OBJS += http.o http-walker.o remote-curl.o
endif
XDIFF_OBJS = xdiff/xdiffi.o xdiff/xprepare.o xdiff/xutils.o xdiff/xemit.o \
	xdiff/xmerge.o xdiff/xpatience.o
VCSSVN_OBJS = vcs-svn/string_pool.o vcs-svn/line_buffer.o \
	vcs-svn/repo_tree.o vcs-svn/fast_export.o vcs-svn/svndump.o
VCSSVN_TEST_OBJS = test-obj-pool.o test-string-pool.o \
	test-line-buffer.o test-treap.o
OBJECTS := $(GIT_OBJS) $(XDIFF_OBJS) $(VCSSVN_OBJS)

dep_files := $(foreach f,$(OBJECTS),$(dir $f).depend/$(notdir $f).d)
dep_dirs := $(addsuffix .depend,$(sort $(dir $(OBJECTS))))

ifdef COMPUTE_HEADER_DEPENDENCIES
$(dep_dirs):
	mkdir -p $@

missing_dep_dirs := $(filter-out $(wildcard $(dep_dirs)),$(dep_dirs))
dep_file = $(dir $@).depend/$(notdir $@).d
dep_args = -MF $(dep_file) -MMD -MP
ifdef CHECK_HEADER_DEPENDENCIES
$(error cannot compute header dependencies outside a normal build. \
Please unset CHECK_HEADER_DEPENDENCIES and try again)
endif
endif

ifndef COMPUTE_HEADER_DEPENDENCIES
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
	$(QUIET_CC)$(CC) -S $(ALL_CFLAGS) $(EXTRA_CPPFLAGS) $<

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
#
# XXX. Please check occasionally that these include all dependencies
# gcc detects!

$(GIT_OBJS): $(LIB_H)
builtin/branch.o builtin/checkout.o builtin/clone.o builtin/reset.o branch.o transport.o: branch.h
builtin/bundle.o bundle.o transport.o: bundle.h
builtin/bisect--helper.o builtin/rev-list.o bisect.o: bisect.h
builtin/clone.o builtin/fetch-pack.o transport.o: fetch-pack.h
builtin/grep.o builtin/pack-objects.o transport-helper.o: thread-utils.h
builtin/send-pack.o transport.o: send-pack.h
builtin/log.o builtin/shortlog.o: shortlog.h
builtin/prune.o builtin/reflog.o reachable.o: reachable.h
builtin/commit.o builtin/revert.o wt-status.o: wt-status.h
builtin/tar-tree.o archive-tar.o: tar.h
connect.o transport.o http-backend.o: url.h
http-fetch.o http-walker.o remote-curl.o transport.o walker.o: walker.h
http.o http-walker.o http-push.o http-fetch.o remote-curl.o: http.h url.h

xdiff-interface.o $(XDIFF_OBJS): \
	xdiff/xinclude.h xdiff/xmacros.h xdiff/xdiff.h xdiff/xtypes.h \
	xdiff/xutils.h xdiff/xprepare.h xdiff/xdiffi.h xdiff/xemit.h

$(VCSSVN_OBJS) $(VCSSVN_TEST_OBJS): $(LIB_H) \
	vcs-svn/obj_pool.h vcs-svn/trp.h vcs-svn/string_pool.h \
	vcs-svn/line_buffer.h vcs-svn/repo_tree.h vcs-svn/fast_export.h \
	vcs-svn/svndump.h

test-svn-fe.o: vcs-svn/svndump.h
endif

exec_cmd.sp exec_cmd.s exec_cmd.o: EXTRA_CPPFLAGS = \
	'-DGIT_EXEC_PATH="$(gitexecdir_SQ)"' \
	'-DBINDIR="$(bindir_relative_SQ)"' \
	'-DPREFIX="$(prefix_SQ)"'

builtin/init-db.sp builtin/init-db.s builtin/init-db.o: EXTRA_CPPFLAGS = \
	-DDEFAULT_GIT_TEMPLATE_DIR='"$(template_dir_SQ)"'

config.sp config.s config.o: EXTRA_CPPFLAGS = \
	-DETC_GITCONFIG='"$(ETC_GITCONFIG_SQ)"'

attr.sp attr.s attr.o: EXTRA_CPPFLAGS = \
	-DETC_GITATTRIBUTES='"$(ETC_GITATTRIBUTES_SQ)"'

http.sp http.s http.o: EXTRA_CPPFLAGS = \
	-DGIT_HTTP_USER_AGENT='"git/$(GIT_VERSION)"'

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
endif

git-%$X: %.o GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) $(LIBS)

git-imap-send$X: imap-send.o GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) \
		$(LIBS) $(OPENSSL_LINK) $(OPENSSL_LIBSSL) $(LIB_4_CRYPTO)

git-http-fetch$X: revision.o http.o http-walker.o http-fetch.o GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) \
		$(LIBS) $(CURL_LIBCURL)
git-http-push$X: revision.o http.o http-push.o GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) \
		$(LIBS) $(CURL_LIBCURL) $(EXPAT_LIBEXPAT)

$(REMOTE_CURL_ALIASES): $(REMOTE_CURL_PRIMARY)
	$(QUIET_LNCP)$(RM) $@ && \
	ln $< $@ 2>/dev/null || \
	ln -s $< $@ 2>/dev/null || \
	cp $< $@

$(REMOTE_CURL_PRIMARY): remote-curl.o http.o http-walker.o GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) \
		$(LIBS) $(CURL_LIBCURL) $(EXPAT_LIBEXPAT)

$(LIB_FILE): $(LIB_OBJS)
	$(QUIET_AR)$(RM) $@ && $(AR) rcs $@ $(LIB_OBJS)

$(XDIFF_LIB): $(XDIFF_OBJS)
	$(QUIET_AR)$(RM) $@ && $(AR) rcs $@ $(XDIFF_OBJS)

$(VCSSVN_LIB): $(VCSSVN_OBJS)
	$(QUIET_AR)$(RM) $@ && $(AR) rcs $@ $(VCSSVN_OBJS)

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
	--add-comments \
	--msgid-bugs-address="Git Mailing List <git@vger.kernel.org>" \
	--from-code=UTF-8
XGETTEXT_FLAGS_C = $(XGETTEXT_FLAGS) --language=C \
	--keyword=_ --keyword=N_ --keyword="Q_:1,2"
XGETTEXT_FLAGS_SH = $(XGETTEXT_FLAGS) --language=Shell
LOCALIZED_C := $(C_OBJ:o=c)
LOCALIZED_SH := $(SCRIPT_SH)

po/git.pot: $(LOCALIZED_C)
	$(QUIET_XGETTEXT)$(XGETTEXT) -o$@+ $(XGETTEXT_FLAGS_C) $(LOCALIZED_C)
	$(QUIET_XGETTEXT)$(XGETTEXT) -o$@+ --join-existing $(XGETTEXT_FLAGS_SH) \
		$(LOCALIZED_SH)
	mv $@+ $@

pot: po/git.pot

$(ETAGS_TARGET): FORCE
	$(RM) $(ETAGS_TARGET)
	$(FIND) . -name '*.[hcS]' -print | xargs etags -a -o $(ETAGS_TARGET)

tags: FORCE
	$(RM) tags
	$(FIND) . -name '*.[hcS]' -print | xargs ctags -a

cscope:
	$(RM) cscope*
	$(FIND) . -name '*.[hcS]' -print | xargs cscope -b

### Detect prefix changes
TRACK_CFLAGS = $(CC):$(subst ','\'',$(ALL_CFLAGS)):\
             $(bindir_SQ):$(gitexecdir_SQ):$(template_dir_SQ):$(prefix_SQ)

GIT-CFLAGS: FORCE
	@FLAGS='$(TRACK_CFLAGS)'; \
	    if test x"$$FLAGS" != x"`cat GIT-CFLAGS 2>/dev/null`" ; then \
		echo 1>&2 "    * new build flags or prefix"; \
		echo "$$FLAGS" >GIT-CFLAGS; \
            fi

TRACK_LDFLAGS = $(subst ','\'',$(ALL_LDFLAGS))

GIT-LDFLAGS: FORCE
	@FLAGS='$(TRACK_LDFLAGS)'; \
	    if test x"$$FLAGS" != x"`cat GIT-LDFLAGS 2>/dev/null`" ; then \
		echo 1>&2 "    * new link flags"; \
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
ifdef GIT_TEST_CMP
	@echo GIT_TEST_CMP=\''$(subst ','\'',$(subst ','\'',$(GIT_TEST_CMP)))'\' >>$@
endif
ifdef GIT_TEST_CMP_USE_COPIED_CONTEXT
	@echo GIT_TEST_CMP_USE_COPIED_CONTEXT=YesPlease >>$@
endif
	@echo GETTEXT_POISON=\''$(subst ','\'',$(subst ','\'',$(GETTEXT_POISON)))'\' >>$@

### Detect Tck/Tk interpreter path changes
ifndef NO_TCLTK
TRACK_VARS = $(subst ','\'',-DTCLTK_PATH='$(TCLTK_PATH_SQ)')

GIT-GUI-VARS: FORCE
	@VARS='$(TRACK_VARS)'; \
	    if test x"$$VARS" != x"`cat $@ 2>/dev/null`" ; then \
		echo 1>&2 "    * new Tcl/Tk interpreter location"; \
		echo "$$VARS" >$@; \
            fi
endif

test_bindir_programs := $(patsubst %,bin-wrappers/%,$(BINDIR_PROGRAMS_NEED_X) $(BINDIR_PROGRAMS_NO_X) $(TEST_PROGRAMS_NEED_X))

all:: $(TEST_PROGRAMS) $(test_bindir_programs)

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

### Testing rules

test: all
	$(MAKE) -C t/ all

test-ctype$X: ctype.o

test-date$X: date.o ctype.o

test-delta$X: diff-delta.o patch-delta.o

test-line-buffer$X: vcs-svn/lib.a

test-parse-options$X: parse-options.o

test-string-pool$X: vcs-svn/lib.a

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

remove-dashes:
	./fixup-builtins $(BUILT_INS) $(PROGRAMS) $(SCRIPTS)

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

install_bindir_programs := $(patsubst %,%$X,$(BINDIR_PROGRAMS_NEED_X)) $(BINDIR_PROGRAMS_NO_X)

install: all
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(bindir_SQ)'
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(gitexec_instdir_SQ)'
	$(INSTALL) $(ALL_PROGRAMS) '$(DESTDIR_SQ)$(gitexec_instdir_SQ)'
	$(INSTALL) -m 644 $(SCRIPT_LIB) '$(DESTDIR_SQ)$(gitexec_instdir_SQ)'
	$(INSTALL) $(install_bindir_programs) '$(DESTDIR_SQ)$(bindir_SQ)'
	$(MAKE) -C templates DESTDIR='$(DESTDIR_SQ)' install
ifndef NO_PERL
	$(MAKE) -C perl prefix='$(prefix_SQ)' DESTDIR='$(DESTDIR_SQ)' install
	$(MAKE) -C gitweb install
endif
ifndef NO_PYTHON
	$(MAKE) -C git_remote_helpers prefix='$(prefix_SQ)' DESTDIR='$(DESTDIR_SQ)' install
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
		test -z "$(NO_CROSS_DIRECTORY_HARDLINKS)" && \
		ln "$$bindir/$$p" "$$execdir/$$p" 2>/dev/null || \
		cp "$$bindir/$$p" "$$execdir/$$p" || exit; \
	  done; \
	} && \
	for p in $(filter $(install_bindir_programs),$(BUILT_INS)); do \
		$(RM) "$$bindir/$$p" && \
		ln "$$bindir/git$X" "$$bindir/$$p" 2>/dev/null || \
		ln -s "git$X" "$$bindir/$$p" 2>/dev/null || \
		cp "$$bindir/git$X" "$$bindir/$$p" || exit; \
	done && \
	for p in $(BUILT_INS); do \
		$(RM) "$$execdir/$$p" && \
		ln "$$execdir/git$X" "$$execdir/$$p" 2>/dev/null || \
		ln -s "git$X" "$$execdir/$$p" 2>/dev/null || \
		cp "$$execdir/git$X" "$$execdir/$$p" || exit; \
	done && \
	remote_curl_aliases="$(REMOTE_CURL_ALIASES)" && \
	for p in $$remote_curl_aliases; do \
		$(RM) "$$execdir/$$p" && \
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

git.spec: git.spec.in
	sed -e 's/@@VERSION@@/$(GIT_VERSION)/g' < $< > $@+
	mv $@+ $@

GIT_TARNAME=git-$(GIT_VERSION)
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
	$(RM) po/git.pot

clean:
	$(RM) *.o block-sha1/*.o ppc/*.o compat/*.o compat/*/*.o xdiff/*.o vcs-svn/*.o \
		builtin/*.o $(LIB_FILE) $(XDIFF_LIB) $(VCSSVN_LIB)
	$(RM) $(ALL_PROGRAMS) $(SCRIPT_LIB) $(BUILT_INS) git$X
	$(RM) $(TEST_PROGRAMS)
	$(RM) -r bin-wrappers
	$(RM) -r $(dep_dirs)
	$(RM) *.spec *.pyc *.pyo */*.pyc */*.pyo common-cmds.h $(ETAGS_TARGET) tags cscope*
	$(RM) -r autom4te.cache
	$(RM) config.log config.mak.autogen config.mak.append config.status config.cache
	$(RM) -r $(GIT_TARNAME) .doc-tmp-dir
	$(RM) $(GIT_TARNAME).tar.gz git-core_$(GIT_VERSION)-*.tar.gz
	$(RM) $(htmldocs).tar.gz $(manpages).tar.gz
	$(MAKE) -C Documentation/ clean
ifndef NO_PERL
	$(MAKE) -C gitweb clean
	$(MAKE) -C perl clean
endif
ifndef NO_PYTHON
	$(MAKE) -C git_remote_helpers clean
endif
	$(MAKE) -C templates/ clean
	$(MAKE) -C t/ clean
ifndef NO_TCLTK
	$(MAKE) -C gitk-git clean
	$(MAKE) -C git-gui clean
endif
	$(RM) GIT-VERSION-FILE GIT-CFLAGS GIT-LDFLAGS GIT-GUI-VARS GIT-BUILD-OPTIONS

.PHONY: all install clean strip
.PHONY: shell_compatibility_test please_set_SHELL_PATH_to_a_more_modern_shell
.PHONY: FORCE cscope

### Check documentation
#
check-docs::
	@(for v in $(ALL_PROGRAMS) $(SCRIPT_LIB) $(BUILT_INS) git gitk; \
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
		ls -1 Documentation/git*txt | \
		sed -e 's|Documentation/|documented |' \
		    -e 's/\.txt//'; \
	) | while read how cmd; \
	do \
		case "$$how,$$cmd" in \
		*,git-citool | \
		*,git-gui | \
		*,git-help | \
		documented,gitattributes | \
		documented,gitignore | \
		documented,gitmodules | \
		documented,gitcli | \
		documented,git-tools | \
		documented,gitcore-tutorial | \
		documented,gitcvs-migration | \
		documented,gitdiffcore | \
		documented,gitglossary | \
		documented,githooks | \
		documented,gitrepository-layout | \
		documented,gitrevisions | \
		documented,gittutorial | \
		documented,gittutorial-2 | \
		documented,git-bisect-lk2009 | \
		documented,git-remote-helpers | \
		documented,gitworkflows | \
		sentinel,not,matching,is,ok ) continue ;; \
		esac; \
		case " $(ALL_PROGRAMS) $(SCRIPT_LIB) $(BUILT_INS) git gitk " in \
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
.PHONY: coverage coverage-clean coverage-build coverage-report

coverage:
	$(MAKE) coverage-build
	$(MAKE) coverage-report

object_dirs := $(sort $(dir $(OBJECTS)))
coverage-clean:
	$(RM) $(addsuffix *.gcov,$(object_dirs))
	$(RM) $(addsuffix *.gcda,$(object_dirs))
	$(RM) $(addsuffix *.gcno,$(object_dirs))
	$(RM) coverage-untested-functions
	$(RM) -r cover_db/
	$(RM) -r cover_db_html/

COVERAGE_CFLAGS = $(CFLAGS) -O0 -ftest-coverage -fprofile-arcs
COVERAGE_LDFLAGS = $(CFLAGS)  -O0 -lgcov
GCOVFLAGS = --preserve-paths --branch-probabilities --all-blocks

coverage-build: coverage-clean
	$(MAKE) CFLAGS="$(COVERAGE_CFLAGS)" LDFLAGS="$(COVERAGE_LDFLAGS)" all
	$(MAKE) CFLAGS="$(COVERAGE_CFLAGS)" LDFLAGS="$(COVERAGE_LDFLAGS)" \
		-j1 test

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

### profile feedback build
#
.PHONY: profile-all profile-clean

PROFILE_GEN_CFLAGS := $(CFLAGS) -fprofile-generate -DNO_NORETURN=1
PROFILE_USE_CFLAGS := $(CFLAGS) -fprofile-use -fprofile-correction -DNO_NORETURN=1

profile-clean:
	$(RM) $(addsuffix *.gcda,$(object_dirs))
	$(RM) $(addsuffix *.gcno,$(object_dirs))

profile-all: profile-clean
	$(MAKE) CFLAGS="$(PROFILE_GEN_CFLAGS)" all
	$(MAKE) CFLAGS="$(PROFILE_GEN_CFLAGS)" -j1 test
	$(MAKE) CFLAGS="$(PROFILE_USE_CFLAGS)" all
