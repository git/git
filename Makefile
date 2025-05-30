# The default target of this Makefile is...
all::

# Import tree-wide shared Makefile behavior and libraries
include shared.mak

# == Makefile defines ==
#
# These defines change the behavior of the Makefile itself, but have
# no impact on what it builds:
#
# Define V=1 to have a more verbose compile.
#
# == Portability and optional library defines ==
#
# These defines indicate what Git can expect from the OS, what
# libraries are available etc. Much of this is auto-detected in
# config.mak.uname, or in configure.ac when using the optional "make
# configure && ./configure" (see INSTALL).
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
# Define SNPRINTF_RETURNS_BOGUS if you are on a system which snprintf()
# or vsnprintf() return -1 instead of number of characters which would
# have been written to the final string if enough space had been available.
#
# Define FREAD_READS_DIRECTORIES if you are on a system which succeeds
# when attempting to read from an fopen'ed directory (or even to fopen
# it at all).
#
# Define OPEN_RETURNS_EINTR if your open() system call may return EINTR
# when a signal is received (as opposed to restarting).
#
# Define NO_OPENSSL environment variable if you do not have OpenSSL.
#
# Define HAVE_ALLOCA_H if you have working alloca(3) defined in that header.
#
# Define HAVE_PATHS_H if you have paths.h and want to use the default PATH
# it specifies.
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
# Define NEEDS_CRYPTO_WITH_SSL if you need -lcrypto when using -lssl (Darwin).
#
# Define NEEDS_SSL_WITH_CRYPTO if you need -lssl when using -lcrypto (Darwin).
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
# Define MMAP_PREVENTS_DELETE if a file that is currently mmapped cannot be
# deleted or cannot be replaced using rename().
#
# Define NO_POLL_H if you don't have poll.h.
#
# Define NO_SYS_POLL_H if you don't have sys/poll.h.
#
# Define NO_POLL if you do not have or don't want to use poll().
# This also implies NO_POLL_H and NO_SYS_POLL_H.
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
# Define CSPRNG_METHOD to "arc4random" if your system has arc4random and
# arc4random_buf, "libbsd" if your system has those functions from libbsd,
# "getrandom" if your system has getrandom, "getentropy" if your system has
# getentropy, "rtlgenrandom" for RtlGenRandom (Windows only), or "openssl" if
# you'd want to use the OpenSSL CSPRNG.  You may set multiple options with
# spaces, in which case a suitable option will be chosen.  If unset or set to
# anything else, defaults to using "/dev/urandom".
#
# Define NEEDS_MODE_TRANSLATION if your OS strays from the typical file type
# bits in mode values (e.g. z/OS defines I_SFMT to 0xFF000000 as opposed to the
# usual 0xF000).
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
# Define ICONV_OMITS_BOM if your iconv implementation does not write a
# byte-order mark (BOM) when writing UTF-16 or UTF-32 and always writes in
# big-endian format.
#
# Define NO_DEFLATE_BOUND if your zlib does not have deflateBound.
#
# Define NO_NORETURN if using buggy versions of gcc 4.6+ and profile feedback,
# as the compiler can crash (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=49299)
#
# Define USE_NSEC below if you want git to care about sub-second file mtimes
# and ctimes. Note that you need recent glibc (at least 2.2.4) for this. On
# Linux, kernel 2.6.11 or newer is required for reliable sub-second file times
# on file systems with exactly 1 ns or 1 s resolution. If you intend to use Git
# on other file systems (e.g. CEPH, CIFS, NTFS, UDF), don't enable USE_NSEC. See
# Documentation/technical/racy-git.txt for details.
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
# Define USE_ASCIIDOCTOR to use Asciidoctor instead of AsciiDoc to build the
# documentation.
#
# Define ASCIIDOCTOR_EXTENSIONS_LAB to point to the location of the Asciidoctor
# Extensions Lab if you have it available.
#
# Define PERL_PATH to the path of your Perl binary (usually /usr/bin/perl).
#
# Define NO_PERL if you do not want Perl scripts or libraries at all.
#
# Define NO_PERL_CPAN_FALLBACKS if you do not want to install bundled
# copies of CPAN modules that serve as a fallback in case the modules
# are not available on the system. This option is intended for
# distributions that want to use their packaged versions of Perl
# modules, instead of the fallbacks shipped with Git.
#
# Define NO_GITWEB if you do not want to build or install
# 'gitweb'. Note that defining NO_PERL currently has the same effect
# on not installing gitweb, but not on whether it's built in the
# gitweb/ directory.
#
# Define PYTHON_PATH to the path of your Python binary (often /usr/bin/python
# but /usr/bin/python2.7 or /usr/bin/python3 on some platforms).
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
# Define HAVE_ISO_QSORT_S if your platform provides a qsort_s() that's
# compatible with the one described in C11 Annex K.
#
# Define UNRELIABLE_FSTAT if your system's fstat does not return the same
# information on a not yet closed file that lstat would return for the same
# file after it was closed.
#
# Define OBJECT_CREATION_USES_RENAMES if your operating systems has problems
# when hardlinking a file to another name and unlinking the original file right
# away (some NTFS drivers seem to zero the contents in that scenario).
#
# Define INSTALL_SYMLINKS if you prefer to have everything that can be
# symlinked between bin/ and libexec/ to use relative symlinks between
# the two. This option overrides NO_CROSS_DIRECTORY_HARDLINKS and
# NO_INSTALL_HARDLINKS which will also use symlinking by indirection
# within the same directory in some cases, INSTALL_SYMLINKS will
# always symlink to the final target directly.
#
# Define NO_CROSS_DIRECTORY_HARDLINKS if you plan to distribute the installed
# programs as a tar, where bin/ and libexec/ might be on different file systems.
#
# Define NO_INSTALL_HARDLINKS if you prefer to use either symbolic links or
# copies to install built-in git commands e.g. git-cat-file.
#
# Define SKIP_DASHED_BUILT_INS if you do not need the dashed versions of the
# built-ins to be linked/copied at all.
#
# Define USE_NED_ALLOCATOR if you want to replace the platforms default
# memory allocators with the nedmalloc allocator written by Niall Douglas.
#
# Define OVERRIDE_STRDUP to override the libc version of strdup(3).
# This is necessary when using a custom allocator in order to avoid
# crashes due to allocation and free working on different 'heaps'.
# It's defined automatically if USE_NED_ALLOCATOR is set.
#
# Define NO_REGEX if your C library lacks regex support with REG_STARTEND
# feature.
#
# Define USE_ENHANCED_BASIC_REGULAR_EXPRESSIONS if your C library provides
# the flag REG_ENHANCED and you'd like to use it to enable enhanced basic
# regular expressions.
#
# Define HAVE_DEV_TTY if your system can open /dev/tty to interact with the
# user.
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
# Define NATIVE_CRLF if your platform uses CRLF for line endings.
#
# Define GIT_USER_AGENT if you want to change how git identifies itself during
# network interactions.  The default is "git/$(GIT_VERSION)".
#
# Define DEFAULT_HELP_FORMAT to "man", "info" or "html"
# (defaults to "man") if you want to have a different default when
# "git help" is called without a parameter specifying the format.
#
# Define GIT_TEST_INDEX_VERSION to 2, 3 or 4 to run the test suite
# with a different indexfile format version.  If it isn't set the index
# file format used is index-v[23].
#
# Define GIT_TEST_UTF8_LOCALE to preferred utf-8 locale for testing.
# If it isn't set, fallback to $LC_ALL, $LANG or use the first utf-8
# locale returned by "locale -a".
#
# Define HAVE_CLOCK_GETTIME if your platform has clock_gettime.
#
# Define HAVE_CLOCK_MONOTONIC if your platform has CLOCK_MONOTONIC.
#
# Define HAVE_SYNC_FILE_RANGE if your platform has sync_file_range.
#
# Define NEEDS_LIBRT if your platform requires linking with librt (glibc version
# before 2.17) for clock_gettime and CLOCK_MONOTONIC.
#
# Define HAVE_BSD_SYSCTL if your platform has a BSD-compatible sysctl function.
#
# Define HAVE_GETDELIM if your system has the getdelim() function.
#
# Define FILENO_IS_A_MACRO if fileno() is a macro, not a real function.
#
# Define NEED_ACCESS_ROOT_HANDLER if access() under root may success for X_OK
# even if execution permission isn't granted for any user.
#
# Define PAGER_ENV to a SP separated VAR=VAL pairs to define
# default environment variables to be passed when a pager is spawned, e.g.
#
#    PAGER_ENV = LESS=FRX LV=-c
#
# to say "export LESS=FRX (and LV=-c) if the environment variable
# LESS (and LV) is not set, respectively".
#
# Define TEST_SHELL_PATH if you want to use a shell besides SHELL_PATH for
# running the test scripts (e.g., bash has better support for "set -x"
# tracing).
#
# When cross-compiling, define HOST_CPU as the canonical name of the CPU on
# which the built Git will run (for instance "x86_64").
#
# Define RUNTIME_PREFIX to configure Git to resolve its ancillary tooling and
# support files relative to the location of the runtime binary, rather than
# hard-coding them into the binary. Git installations built with RUNTIME_PREFIX
# can be moved to arbitrary filesystem locations. RUNTIME_PREFIX also causes
# Perl scripts to use a modified entry point header allowing them to resolve
# support files at runtime.
#
# When using RUNTIME_PREFIX, define HAVE_BSD_KERN_PROC_SYSCTL if your platform
# supports the KERN_PROC BSD sysctl function.
#
# When using RUNTIME_PREFIX, define PROCFS_EXECUTABLE_PATH if your platform
# mounts a "procfs" filesystem capable of resolving the path of the current
# executable. If defined, this must be the canonical path for the "procfs"
# current executable path.
#
# When using RUNTIME_PREFIX, define HAVE_NS_GET_EXECUTABLE_PATH if your platform
# supports calling _NSGetExecutablePath to retrieve the path of the running
# executable.
#
# When using RUNTIME_PREFIX, define HAVE_ZOS_GET_EXECUTABLE_PATH if your platform
# supports calling __getprogramdir and getprogname to retrieve the path of the
# running executable.
#
# When using RUNTIME_PREFIX, define HAVE_WPGMPTR if your platform offers
# the global variable _wpgmptr containing the absolute path of the current
# executable (this is the case on Windows).
#
# INSTALL_STRIP can be set to "-s" to strip binaries during installation,
# if your $(INSTALL) command supports the option.
#
# Define GENERATE_COMPILATION_DATABASE to "yes" to generate JSON compilation
# database entries during compilation if your compiler supports it, using the
# `-MJ` flag. The JSON entries will be placed in the `compile_commands/`
# directory, and the JSON compilation database 'compile_commands.json' will be
# created at the root of the repository.
#
# If your platform supports a built-in fsmonitor backend, set
# FSMONITOR_DAEMON_BACKEND to the "<name>" of the corresponding
# `compat/fsmonitor/fsm-listen-<name>.c` and
# `compat/fsmonitor/fsm-health-<name>.c` files
# that implement the `fsm_listen__*()` and `fsm_health__*()` routines.
#
# If your platform has OS-specific ways to tell if a repo is incompatible with
# fsmonitor (whether the hook or IPC daemon version), set FSMONITOR_OS_SETTINGS
# to the "<name>" of the corresponding `compat/fsmonitor/fsm-settings-<name>.c`
# that implements the `fsm_os_settings__*()` routines.
#
# Define LINK_FUZZ_PROGRAMS if you want `make all` to also build the fuzz test
# programs in oss-fuzz/.
#
# === Optional library: libintl ===
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
# === Optional library: libexpat ===
#
# Define NO_EXPAT if you do not have expat installed.  git-http-push is
# not built, and you cannot push using http:// and https:// transports (dumb).
#
# Define EXPATDIR=/foo/bar if your expat header and library files are in
# /foo/bar/include and /foo/bar/lib directories.
#
# Define EXPAT_NEEDS_XMLPARSE_H if you have an old version of expat (e.g.,
# 1.1 or 1.2) that provides xmlparse.h instead of expat.h.

# === Optional library: libcurl ===
#
# Define NO_CURL if you do not have libcurl installed.  git-http-fetch and
# git-http-push are not built, and you cannot use http:// and https://
# transports (neither smart nor dumb).
#
# Define CURLDIR=/foo/bar if your curl header and library files are in
# /foo/bar/include and /foo/bar/lib directories.
#
# Define CURL_CONFIG to curl's configuration program that prints information
# about the library (e.g., its version number).  The default is 'curl-config'.
#
# Define CURL_LDFLAGS to specify flags that you need to link when using libcurl,
# if you do not want to rely on the libraries provided by CURL_CONFIG.  The
# default value is a result of `curl-config --libs`.  An example value for
# CURL_LDFLAGS is as follows:
#
#     CURL_LDFLAGS=-lcurl
#
# === Optional library: libpcre2 ===
#
# Define USE_LIBPCRE if you have and want to use libpcre. Various
# commands such as log and grep offer runtime options to use
# Perl-compatible regular expressions instead of standard or extended
# POSIX regular expressions.
#
# Only libpcre version 2 is supported. USE_LIBPCRE2 is a synonym for
# USE_LIBPCRE, support for the old USE_LIBPCRE1 has been removed.
#
# Define LIBPCREDIR=/foo/bar if your PCRE header and library files are
# in /foo/bar/include and /foo/bar/lib directories.
#
# == SHA-1 and SHA-256 defines ==
#
# === SHA-1 backend ===
#
# ==== Security ====
#
# Due to the SHAttered (https://shattered.io) attack vector on SHA-1
# it's strongly recommended to use the sha1collisiondetection
# counter-cryptanalysis library for SHA-1 hashing.
#
# If you know that you can trust the repository contents, or where
# potential SHA-1 attacks are otherwise mitigated the other backends
# listed in "SHA-1 implementations" are faster than
# sha1collisiondetection.
#
# ==== Default SHA-1 backend ====
#
# If no *_SHA1 backend is picked, the first supported one listed in
# "SHA-1 implementations" will be picked.
#
# ==== Options common to all SHA-1 implementations ====
#
# Define SHA1_MAX_BLOCK_SIZE to limit the amount of data that will be hashed
# in one call to the platform's SHA1_Update(). e.g. APPLE_COMMON_CRYPTO
# wants 'SHA1_MAX_BLOCK_SIZE=1024L*1024L*1024L' defined.
#
# ==== SHA-1 implementations ====
#
# Define OPENSSL_SHA1 to link to the SHA-1 routines from the OpenSSL
# library.
#
# Define BLK_SHA1 to make use of optimized C SHA-1 routines bundled
# with git (in the block-sha1/ directory).
#
# Define APPLE_COMMON_CRYPTO_SHA1 to use Apple's CommonCrypto for
# SHA-1.
#
# Define the same Makefile knobs as above, but suffixed with _UNSAFE to
# use the corresponding implementations for unsafe SHA-1 hashing for
# non-cryptographic purposes.
#
# If don't enable any of the *_SHA1 settings in this section, Git will
# default to its built-in sha1collisiondetection library, which is a
# collision-detecting sha1 This is slower, but may detect attempted
# collision attacks.
#
# ==== Options for the sha1collisiondetection library ====
#
# Define DC_SHA1_EXTERNAL if you want to build / link
# git with the external SHA1 collision-detect library.
# Without this option, i.e. the default behavior is to build git with its
# own built-in code (or submodule).
#
# Define DC_SHA1_SUBMODULE to use the
# sha1collisiondetection shipped as a submodule instead of the
# non-submodule copy in sha1dc/. This is an experimental option used
# by the git project to migrate to using sha1collisiondetection as a
# submodule.
#
# === SHA-256 backend ===
#
# ==== Security ====
#
# Unlike SHA-1 the SHA-256 algorithm does not suffer from any known
# vulnerabilities, so any implementation will do.
#
# ==== SHA-256 implementations ====
#
# Define OPENSSL_SHA256 to use the SHA-256 routines in OpenSSL.
#
# Define NETTLE_SHA256 to use the SHA-256 routines in libnettle.
#
# Define GCRYPT_SHA256 to use the SHA-256 routines in libgcrypt.
#
# If don't enable any of the *_SHA256 settings in this section, Git
# will default to its built-in sha256 implementation.
#
# == DEVELOPER defines ==
#
# Define DEVELOPER to enable more compiler warnings. Compiler version
# and family are auto detected, but could be overridden by defining
# COMPILER_FEATURES (see config.mak.dev). You can still set
# CFLAGS="..." in combination with DEVELOPER enables, whether that's
# for tweaking something unrelated (e.g. optimization level), or for
# selectively overriding something DEVELOPER or one of the DEVOPTS
# (see just below) brings in.
#
# When DEVELOPER is set, DEVOPTS can be used to control compiler
# options.  This variable contains keywords separated by
# whitespace. The following keywords are recognized:
#
#    no-error:
#
#        suppresses the -Werror that implicitly comes with
#        DEVELOPER=1. Useful for getting the full set of errors
#        without immediately dying, or for logging them.
#
#    extra-all:
#
#        The DEVELOPER mode enables -Wextra with a few exceptions. By
#        setting this flag the exceptions are removed, and all of
#        -Wextra is used.
#
#    no-pedantic:
#
#        Disable -pedantic compilation.

GIT-VERSION-FILE: FORCE
	@$(SHELL_PATH) ./GIT-VERSION-GEN
-include GIT-VERSION-FILE

# Set our default configuration.
#
# Among the variables below, these:
#   gitexecdir
#   template_dir
#   sysconfdir
# can be specified as a relative path some/where/else;
# this is interpreted as relative to $(prefix) and "git" built with
# RUNTIME_PREFIX flag will figure out (at runtime) where they are
# based on the path to the executable.
# Additionally, the following will be treated as relative by "git" if they
# begin with "$(prefix)/":
#   mandir
#   infodir
#   htmldir
#   localedir
#   perllibdir
# This can help installing the suite in a relocatable way.

prefix = $(HOME)
bindir = $(prefix)/bin
mandir = $(prefix)/share/man
infodir = $(prefix)/share/info
gitexecdir = libexec/git-core
mergetoolsdir = $(gitexecdir)/mergetools
sharedir = $(prefix)/share
gitwebdir = $(sharedir)/gitweb
gitwebstaticdir = $(gitwebdir)/static
perllibdir = $(sharedir)/perl5
localedir = $(sharedir)/locale
template_dir = share/git-core/templates
htmldir = $(prefix)/share/doc/git-doc
ETC_GITCONFIG = $(sysconfdir)/gitconfig
ETC_GITATTRIBUTES = $(sysconfdir)/gitattributes
lib = lib
# DESTDIR =
pathsep = :

bindir_relative = $(patsubst $(prefix)/%,%,$(bindir))
mandir_relative = $(patsubst $(prefix)/%,%,$(mandir))
infodir_relative = $(patsubst $(prefix)/%,%,$(infodir))
gitexecdir_relative = $(patsubst $(prefix)/%,%,$(gitexecdir))
localedir_relative = $(patsubst $(prefix)/%,%,$(localedir))
htmldir_relative = $(patsubst $(prefix)/%,%,$(htmldir))
perllibdir_relative = $(patsubst $(prefix)/%,%,$(perllibdir))

export prefix bindir sharedir sysconfdir perllibdir localedir

# Set our default programs
CC = cc
AR = ar
RM = rm -f
DIFF = diff
TAR = tar
FIND = find
INSTALL = install
TCL_PATH = tclsh
TCLTK_PATH = wish
XGETTEXT = xgettext
MSGCAT = msgcat
MSGFMT = msgfmt
MSGMERGE = msgmerge
CURL_CONFIG = curl-config
GCOV = gcov
STRIP = strip
SPATCH = spatch

export TCL_PATH TCLTK_PATH

# Set our default LIBS variables
PTHREAD_LIBS = -lpthread

# Guard against environment variables
BUILTIN_OBJS =
BUILT_INS =
COMPAT_CFLAGS =
COMPAT_OBJS =
XDIFF_OBJS =
GENERATED_H =
EXTRA_CPPFLAGS =
FUZZ_OBJS =
FUZZ_PROGRAMS =
GIT_OBJS =
LIB_OBJS =
SCALAR_OBJS =
OBJECTS =
OTHER_PROGRAMS =
PROGRAM_OBJS =
PROGRAMS =
EXCLUDED_PROGRAMS =
SCRIPT_PERL =
SCRIPT_PYTHON =
SCRIPT_SH =
SCRIPT_LIB =
TEST_BUILTINS_OBJS =
TEST_OBJS =
TEST_PROGRAMS_NEED_X =
THIRD_PARTY_SOURCES =
UNIT_TEST_PROGRAMS =
UNIT_TEST_DIR = t/unit-tests
UNIT_TEST_BIN = $(UNIT_TEST_DIR)/bin

# Having this variable in your environment would break pipelines because
# you cause "cd" to echo its destination to stdout.  It can also take
# scripts to unexpected places.  If you like CDPATH, define it for your
# interactive shell sessions without exporting it.
unexport CDPATH

SCRIPT_SH += git-difftool--helper.sh
SCRIPT_SH += git-filter-branch.sh
SCRIPT_SH += git-merge-octopus.sh
SCRIPT_SH += git-merge-one-file.sh
SCRIPT_SH += git-merge-resolve.sh
SCRIPT_SH += git-mergetool.sh
SCRIPT_SH += git-quiltimport.sh
SCRIPT_SH += git-request-pull.sh
SCRIPT_SH += git-submodule.sh
SCRIPT_SH += git-web--browse.sh

SCRIPT_LIB += git-mergetool--lib
SCRIPT_LIB += git-sh-i18n
SCRIPT_LIB += git-sh-setup

SCRIPT_PERL += git-archimport.perl
SCRIPT_PERL += git-cvsexportcommit.perl
SCRIPT_PERL += git-cvsimport.perl
SCRIPT_PERL += git-cvsserver.perl
SCRIPT_PERL += git-send-email.perl
SCRIPT_PERL += git-svn.perl

SCRIPT_PYTHON += git-p4.py

# Generated files for scripts
SCRIPT_SH_GEN = $(patsubst %.sh,%,$(SCRIPT_SH))
SCRIPT_PERL_GEN = $(patsubst %.perl,%,$(SCRIPT_PERL))
SCRIPT_PYTHON_GEN = $(patsubst %.py,%,$(SCRIPT_PYTHON))

# Individual rules to allow e.g.
# "make -C ../.. SCRIPT_PERL=contrib/foo/bar.perl build-perl-script"
# from subdirectories like contrib/*/
.PHONY: build-perl-script build-sh-script build-python-script
build-perl-script: $(SCRIPT_PERL_GEN)
build-sh-script: $(SCRIPT_SH_GEN)
build-python-script: $(SCRIPT_PYTHON_GEN)

.PHONY: install-perl-script install-sh-script install-python-script
install-sh-script: $(SCRIPT_SH_GEN)
	$(INSTALL) $^ '$(DESTDIR_SQ)$(gitexec_instdir_SQ)'
install-perl-script: $(SCRIPT_PERL_GEN)
	$(INSTALL) $^ '$(DESTDIR_SQ)$(gitexec_instdir_SQ)'
install-python-script: $(SCRIPT_PYTHON_GEN)
	$(INSTALL) $^ '$(DESTDIR_SQ)$(gitexec_instdir_SQ)'

.PHONY: clean-perl-script clean-sh-script clean-python-script
clean-sh-script:
	$(RM) $(SCRIPT_SH_GEN)
clean-perl-script:
	$(RM) $(SCRIPT_PERL_GEN)
clean-python-script:
	$(RM) $(SCRIPT_PYTHON_GEN)

SCRIPTS = $(SCRIPT_SH_GEN) \
	  $(SCRIPT_PERL_GEN) \
	  $(SCRIPT_PYTHON_GEN) \
	  git-instaweb

ETAGS_TARGET = TAGS

# Empty...
EXTRA_PROGRAMS =

# ... and all the rest that could be moved out of bindir to gitexecdir
PROGRAMS += $(EXTRA_PROGRAMS)

PROGRAM_OBJS += daemon.o
PROGRAM_OBJS += http-backend.o
PROGRAM_OBJS += imap-send.o
PROGRAM_OBJS += sh-i18n--envsubst.o
PROGRAM_OBJS += shell.o
.PHONY: program-objs
program-objs: $(PROGRAM_OBJS)

# Binary suffix, set to .exe for Windows builds
X =

PROGRAMS += $(patsubst %.o,git-%$X,$(PROGRAM_OBJS))

TEST_BUILTINS_OBJS += test-advise.o
TEST_BUILTINS_OBJS += test-bitmap.o
TEST_BUILTINS_OBJS += test-bloom.o
TEST_BUILTINS_OBJS += test-bundle-uri.o
TEST_BUILTINS_OBJS += test-cache-tree.o
TEST_BUILTINS_OBJS += test-chmtime.o
TEST_BUILTINS_OBJS += test-config.o
TEST_BUILTINS_OBJS += test-crontab.o
TEST_BUILTINS_OBJS += test-csprng.o
TEST_BUILTINS_OBJS += test-date.o
TEST_BUILTINS_OBJS += test-delete-gpgsig.o
TEST_BUILTINS_OBJS += test-delta.o
TEST_BUILTINS_OBJS += test-dir-iterator.o
TEST_BUILTINS_OBJS += test-drop-caches.o
TEST_BUILTINS_OBJS += test-dump-cache-tree.o
TEST_BUILTINS_OBJS += test-dump-fsmonitor.o
TEST_BUILTINS_OBJS += test-dump-split-index.o
TEST_BUILTINS_OBJS += test-dump-untracked-cache.o
TEST_BUILTINS_OBJS += test-env-helper.o
TEST_BUILTINS_OBJS += test-example-tap.o
TEST_BUILTINS_OBJS += test-find-pack.o
TEST_BUILTINS_OBJS += test-fsmonitor-client.o
TEST_BUILTINS_OBJS += test-genrandom.o
TEST_BUILTINS_OBJS += test-genzeros.o
TEST_BUILTINS_OBJS += test-getcwd.o
TEST_BUILTINS_OBJS += test-hash-speed.o
TEST_BUILTINS_OBJS += test-hash.o
TEST_BUILTINS_OBJS += test-hashmap.o
TEST_BUILTINS_OBJS += test-hexdump.o
TEST_BUILTINS_OBJS += test-json-writer.o
TEST_BUILTINS_OBJS += test-lazy-init-name-hash.o
TEST_BUILTINS_OBJS += test-match-trees.o
TEST_BUILTINS_OBJS += test-mergesort.o
TEST_BUILTINS_OBJS += test-mktemp.o
TEST_BUILTINS_OBJS += test-online-cpus.o
TEST_BUILTINS_OBJS += test-pack-mtimes.o
TEST_BUILTINS_OBJS += test-parse-options.o
TEST_BUILTINS_OBJS += test-parse-pathspec-file.o
TEST_BUILTINS_OBJS += test-partial-clone.o
TEST_BUILTINS_OBJS += test-path-utils.o
TEST_BUILTINS_OBJS += test-path-walk.o
TEST_BUILTINS_OBJS += test-pcre2-config.o
TEST_BUILTINS_OBJS += test-pkt-line.o
TEST_BUILTINS_OBJS += test-proc-receive.o
TEST_BUILTINS_OBJS += test-progress.o
TEST_BUILTINS_OBJS += test-reach.o
TEST_BUILTINS_OBJS += test-read-cache.o
TEST_BUILTINS_OBJS += test-read-graph.o
TEST_BUILTINS_OBJS += test-read-midx.o
TEST_BUILTINS_OBJS += test-ref-store.o
TEST_BUILTINS_OBJS += test-reftable.o
TEST_BUILTINS_OBJS += test-regex.o
TEST_BUILTINS_OBJS += test-rot13-filter.o
TEST_BUILTINS_OBJS += test-repository.o
TEST_BUILTINS_OBJS += test-revision-walking.o
TEST_BUILTINS_OBJS += test-run-command.o
TEST_BUILTINS_OBJS += test-scrap-cache-tree.o
TEST_BUILTINS_OBJS += test-serve-v2.o
TEST_BUILTINS_OBJS += test-sha1.o
TEST_BUILTINS_OBJS += test-sha256.o
TEST_BUILTINS_OBJS += test-sigchain.o
TEST_BUILTINS_OBJS += test-simple-ipc.o
TEST_BUILTINS_OBJS += test-string-list.o
TEST_BUILTINS_OBJS += test-submodule-config.o
TEST_BUILTINS_OBJS += test-submodule-nested-repo-config.o
TEST_BUILTINS_OBJS += test-submodule.o
TEST_BUILTINS_OBJS += test-subprocess.o
TEST_BUILTINS_OBJS += test-trace2.o
TEST_BUILTINS_OBJS += test-truncate.o
TEST_BUILTINS_OBJS += test-userdiff.o
TEST_BUILTINS_OBJS += test-wildmatch.o
TEST_BUILTINS_OBJS += test-windows-named-pipe.o
TEST_BUILTINS_OBJS += test-write-cache.o
TEST_BUILTINS_OBJS += test-xml-encode.o

# Do not add more tests here unless they have extra dependencies. Add
# them in TEST_BUILTINS_OBJS above.
TEST_PROGRAMS_NEED_X += test-fake-ssh
TEST_PROGRAMS_NEED_X += test-tool

TEST_PROGRAMS = $(patsubst %,t/helper/%$X,$(TEST_PROGRAMS_NEED_X))

# List built-in command $C whose implementation cmd_$C() is not in
# builtin/$C.o but is linked in as part of some other command.
BUILT_INS += $(patsubst builtin/%.o,git-%$X,$(BUILTIN_OBJS))

BUILT_INS += git-cherry$X
BUILT_INS += git-cherry-pick$X
BUILT_INS += git-format-patch$X
BUILT_INS += git-fsck-objects$X
BUILT_INS += git-init$X
BUILT_INS += git-maintenance$X
BUILT_INS += git-merge-subtree$X
BUILT_INS += git-restore$X
BUILT_INS += git-show$X
BUILT_INS += git-stage$X
BUILT_INS += git-status$X
BUILT_INS += git-switch$X
BUILT_INS += git-version$X
BUILT_INS += git-whatchanged$X

# what 'all' will build but not install in gitexecdir
OTHER_PROGRAMS += git$X
OTHER_PROGRAMS += scalar$X

# what test wrappers are needed and 'install' will install, in bindir
BINDIR_PROGRAMS_NEED_X += git
BINDIR_PROGRAMS_NEED_X += scalar
BINDIR_PROGRAMS_NEED_X += git-receive-pack
BINDIR_PROGRAMS_NEED_X += git-shell
BINDIR_PROGRAMS_NEED_X += git-upload-archive
BINDIR_PROGRAMS_NEED_X += git-upload-pack

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

TEST_SHELL_PATH = $(SHELL_PATH)

LIB_FILE = libgit.a
XDIFF_LIB = xdiff/lib.a
REFTABLE_LIB = reftable/libreftable.a

GENERATED_H += command-list.h
GENERATED_H += config-list.h
GENERATED_H += hook-list.h
GENERATED_H += $(UNIT_TEST_DIR)/clar-decls.h
GENERATED_H += $(UNIT_TEST_DIR)/clar.suite

.PHONY: generated-hdrs
generated-hdrs: $(GENERATED_H)

## Exhaustive lists of our source files, either dynamically generated,
## or hardcoded.
SOURCES_CMD = ( \
	git ls-files --deduplicate \
		'*.[hcS]' \
		'*.sh' \
		':!*[tp][0-9][0-9][0-9][0-9]*' \
		':!contrib' \
		2>/dev/null || \
	$(FIND) . \
		\( -name .git -type d -prune \) \
		-o \( -name '[tp][0-9][0-9][0-9][0-9]*' -prune \) \
		-o \( -name contrib -type d -prune \) \
		-o \( -name build -type d -prune \) \
		-o \( -name .build -type d -prune \) \
		-o \( -name 'trash*' -type d -prune \) \
		-o \( -name '*.[hcS]' -type f -print \) \
		-o \( -name '*.sh' -type f -print \) \
		| sed -e 's|^\./||' \
	)
FOUND_SOURCE_FILES := $(filter-out $(GENERATED_H),$(shell $(SOURCES_CMD)))

FOUND_C_SOURCES = $(filter %.c,$(FOUND_SOURCE_FILES))
FOUND_H_SOURCES = $(filter %.h,$(FOUND_SOURCE_FILES))

COCCI_SOURCES = $(filter-out $(THIRD_PARTY_SOURCES),$(FOUND_C_SOURCES))

LIB_H = $(FOUND_H_SOURCES)

LIB_OBJS += abspath.o
LIB_OBJS += add-interactive.o
LIB_OBJS += add-patch.o
LIB_OBJS += advice.o
LIB_OBJS += alias.o
LIB_OBJS += alloc.o
LIB_OBJS += apply.o
LIB_OBJS += archive-tar.o
LIB_OBJS += archive-zip.o
LIB_OBJS += archive.o
LIB_OBJS += attr.o
LIB_OBJS += base85.o
LIB_OBJS += bisect.o
LIB_OBJS += blame.o
LIB_OBJS += blob.o
LIB_OBJS += bloom.o
LIB_OBJS += branch.o
LIB_OBJS += bulk-checkin.o
LIB_OBJS += bundle-uri.o
LIB_OBJS += bundle.o
LIB_OBJS += cache-tree.o
LIB_OBJS += cbtree.o
LIB_OBJS += chdir-notify.o
LIB_OBJS += checkout.o
LIB_OBJS += chunk-format.o
LIB_OBJS += color.o
LIB_OBJS += column.o
LIB_OBJS += combine-diff.o
LIB_OBJS += commit-graph.o
LIB_OBJS += commit-reach.o
LIB_OBJS += commit.o
LIB_OBJS += compat/nonblock.o
LIB_OBJS += compat/obstack.o
LIB_OBJS += compat/terminal.o
LIB_OBJS += compat/zlib-uncompress2.o
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
LIB_OBJS += delta-islands.o
LIB_OBJS += diagnose.o
LIB_OBJS += diff-delta.o
LIB_OBJS += diff-merges.o
LIB_OBJS += diff-lib.o
LIB_OBJS += diff-no-index.o
LIB_OBJS += diff.o
LIB_OBJS += diffcore-break.o
LIB_OBJS += diffcore-delta.o
LIB_OBJS += diffcore-order.o
LIB_OBJS += diffcore-pickaxe.o
LIB_OBJS += diffcore-rename.o
LIB_OBJS += diffcore-rotate.o
LIB_OBJS += dir-iterator.o
LIB_OBJS += dir.o
LIB_OBJS += editor.o
LIB_OBJS += entry.o
LIB_OBJS += environment.o
LIB_OBJS += ewah/bitmap.o
LIB_OBJS += ewah/ewah_bitmap.o
LIB_OBJS += ewah/ewah_io.o
LIB_OBJS += ewah/ewah_rlw.o
LIB_OBJS += exec-cmd.o
LIB_OBJS += fetch-negotiator.o
LIB_OBJS += fetch-pack.o
LIB_OBJS += fetch-object-info.o
LIB_OBJS += fmt-merge-msg.o
LIB_OBJS += fsck.o
LIB_OBJS += fsmonitor.o
LIB_OBJS += fsmonitor-ipc.o
LIB_OBJS += fsmonitor-settings.o
LIB_OBJS += gettext.o
LIB_OBJS += git-zlib.o
LIB_OBJS += gpg-interface.o
LIB_OBJS += graph.o
LIB_OBJS += grep.o
LIB_OBJS += hash-lookup.o
LIB_OBJS += hashmap.o
LIB_OBJS += help.o
LIB_OBJS += hex.o
LIB_OBJS += hex-ll.o
LIB_OBJS += hook.o
LIB_OBJS += ident.o
LIB_OBJS += json-writer.o
LIB_OBJS += kwset.o
LIB_OBJS += levenshtein.o
LIB_OBJS += line-log.o
LIB_OBJS += line-range.o
LIB_OBJS += linear-assignment.o
LIB_OBJS += list-objects-filter-options.o
LIB_OBJS += list-objects-filter.o
LIB_OBJS += list-objects.o
LIB_OBJS += lockfile.o
LIB_OBJS += log-tree.o
LIB_OBJS += loose.o
LIB_OBJS += ls-refs.o
LIB_OBJS += mailinfo.o
LIB_OBJS += mailmap.o
LIB_OBJS += match-trees.o
LIB_OBJS += mem-pool.o
LIB_OBJS += merge-blobs.o
LIB_OBJS += merge-ll.o
LIB_OBJS += merge-ort.o
LIB_OBJS += merge-ort-wrappers.o
LIB_OBJS += merge-recursive.o
LIB_OBJS += merge.o
LIB_OBJS += midx.o
LIB_OBJS += midx-write.o
LIB_OBJS += name-hash.o
LIB_OBJS += negotiator/default.o
LIB_OBJS += negotiator/noop.o
LIB_OBJS += negotiator/skipping.o
LIB_OBJS += notes-cache.o
LIB_OBJS += notes-merge.o
LIB_OBJS += notes-utils.o
LIB_OBJS += notes.o
LIB_OBJS += object-file-convert.o
LIB_OBJS += object-file.o
LIB_OBJS += object-name.o
LIB_OBJS += object.o
LIB_OBJS += oid-array.o
LIB_OBJS += oidmap.o
LIB_OBJS += oidset.o
LIB_OBJS += oidtree.o
LIB_OBJS += pack-bitmap-write.o
LIB_OBJS += pack-bitmap.o
LIB_OBJS += pack-check.o
LIB_OBJS += pack-mtimes.o
LIB_OBJS += pack-objects.o
LIB_OBJS += pack-revindex.o
LIB_OBJS += pack-write.o
LIB_OBJS += packfile.o
LIB_OBJS += pager.o
LIB_OBJS += parallel-checkout.o
LIB_OBJS += parse.o
LIB_OBJS += parse-options-cb.o
LIB_OBJS += parse-options.o
LIB_OBJS += patch-delta.o
LIB_OBJS += patch-ids.o
LIB_OBJS += path.o
LIB_OBJS += path-walk.o
LIB_OBJS += pathspec.o
LIB_OBJS += pkt-line.o
LIB_OBJS += preload-index.o
LIB_OBJS += pretty.o
LIB_OBJS += prio-queue.o
LIB_OBJS += progress.o
LIB_OBJS += promisor-remote.o
LIB_OBJS += prompt.o
LIB_OBJS += protocol.o
LIB_OBJS += protocol-caps.o
LIB_OBJS += prune-packed.o
LIB_OBJS += pseudo-merge.o
LIB_OBJS += quote.o
LIB_OBJS += range-diff.o
LIB_OBJS += reachable.o
LIB_OBJS += read-cache.o
LIB_OBJS += rebase-interactive.o
LIB_OBJS += rebase.o
LIB_OBJS += ref-filter.o
LIB_OBJS += reflog-walk.o
LIB_OBJS += reflog.o
LIB_OBJS += refs.o
LIB_OBJS += refs/debug.o
LIB_OBJS += refs/files-backend.o
LIB_OBJS += refs/reftable-backend.o
LIB_OBJS += refs/iterator.o
LIB_OBJS += refs/packed-backend.o
LIB_OBJS += refs/ref-cache.o
LIB_OBJS += refspec.o
LIB_OBJS += remote.o
LIB_OBJS += replace-object.o
LIB_OBJS += repo-settings.o
LIB_OBJS += repository.o
LIB_OBJS += rerere.o
LIB_OBJS += reset.o
LIB_OBJS += resolve-undo.o
LIB_OBJS += revision.o
LIB_OBJS += run-command.o
LIB_OBJS += send-pack.o
LIB_OBJS += sequencer.o
LIB_OBJS += serve.o
LIB_OBJS += server-info.o
LIB_OBJS += setup.o
LIB_OBJS += shallow.o
LIB_OBJS += sideband.o
LIB_OBJS += sigchain.o
LIB_OBJS += sparse-index.o
LIB_OBJS += split-index.o
LIB_OBJS += stable-qsort.o
LIB_OBJS += statinfo.o
LIB_OBJS += strbuf.o
LIB_OBJS += streaming.o
LIB_OBJS += string-list.o
LIB_OBJS += strmap.o
LIB_OBJS += strvec.o
LIB_OBJS += sub-process.o
LIB_OBJS += submodule-config.o
LIB_OBJS += submodule.o
LIB_OBJS += symlinks.o
LIB_OBJS += tag.o
LIB_OBJS += tempfile.o
LIB_OBJS += thread-utils.o
LIB_OBJS += tmp-objdir.o
LIB_OBJS += trace.o
LIB_OBJS += trace2.o
LIB_OBJS += trace2/tr2_cfg.o
LIB_OBJS += trace2/tr2_cmd_name.o
LIB_OBJS += trace2/tr2_ctr.o
LIB_OBJS += trace2/tr2_dst.o
LIB_OBJS += trace2/tr2_sid.o
LIB_OBJS += trace2/tr2_sysenv.o
LIB_OBJS += trace2/tr2_tbuf.o
LIB_OBJS += trace2/tr2_tgt_event.o
LIB_OBJS += trace2/tr2_tgt_normal.o
LIB_OBJS += trace2/tr2_tgt_perf.o
LIB_OBJS += trace2/tr2_tls.o
LIB_OBJS += trace2/tr2_tmr.o
LIB_OBJS += trailer.o
LIB_OBJS += transport-helper.o
LIB_OBJS += transport.o
LIB_OBJS += tree-diff.o
LIB_OBJS += tree-walk.o
LIB_OBJS += tree.o
LIB_OBJS += unpack-trees.o
LIB_OBJS += upload-pack.o
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
LIB_OBJS += worktree.o
LIB_OBJS += wrapper.o
LIB_OBJS += write-or-die.o
LIB_OBJS += ws.o
LIB_OBJS += wt-status.o
LIB_OBJS += xdiff-interface.o

BUILTIN_OBJS += builtin/add.o
BUILTIN_OBJS += builtin/am.o
BUILTIN_OBJS += builtin/annotate.o
BUILTIN_OBJS += builtin/apply.o
BUILTIN_OBJS += builtin/archive.o
BUILTIN_OBJS += builtin/bisect.o
BUILTIN_OBJS += builtin/blame.o
BUILTIN_OBJS += builtin/branch.o
BUILTIN_OBJS += builtin/bugreport.o
BUILTIN_OBJS += builtin/bundle.o
BUILTIN_OBJS += builtin/cat-file.o
BUILTIN_OBJS += builtin/check-attr.o
BUILTIN_OBJS += builtin/check-ignore.o
BUILTIN_OBJS += builtin/check-mailmap.o
BUILTIN_OBJS += builtin/check-ref-format.o
BUILTIN_OBJS += builtin/checkout--worker.o
BUILTIN_OBJS += builtin/checkout-index.o
BUILTIN_OBJS += builtin/checkout.o
BUILTIN_OBJS += builtin/clean.o
BUILTIN_OBJS += builtin/clone.o
BUILTIN_OBJS += builtin/column.o
BUILTIN_OBJS += builtin/commit-graph.o
BUILTIN_OBJS += builtin/commit-tree.o
BUILTIN_OBJS += builtin/commit.o
BUILTIN_OBJS += builtin/config.o
BUILTIN_OBJS += builtin/count-objects.o
BUILTIN_OBJS += builtin/credential-cache--daemon.o
BUILTIN_OBJS += builtin/credential-cache.o
BUILTIN_OBJS += builtin/credential-store.o
BUILTIN_OBJS += builtin/credential.o
BUILTIN_OBJS += builtin/describe.o
BUILTIN_OBJS += builtin/diagnose.o
BUILTIN_OBJS += builtin/diff-files.o
BUILTIN_OBJS += builtin/diff-index.o
BUILTIN_OBJS += builtin/diff-tree.o
BUILTIN_OBJS += builtin/diff.o
BUILTIN_OBJS += builtin/difftool.o
BUILTIN_OBJS += builtin/fast-export.o
BUILTIN_OBJS += builtin/fast-import.o
BUILTIN_OBJS += builtin/fetch-pack.o
BUILTIN_OBJS += builtin/fetch.o
BUILTIN_OBJS += builtin/fmt-merge-msg.o
BUILTIN_OBJS += builtin/for-each-ref.o
BUILTIN_OBJS += builtin/for-each-repo.o
BUILTIN_OBJS += builtin/fsck.o
BUILTIN_OBJS += builtin/fsmonitor--daemon.o
BUILTIN_OBJS += builtin/gc.o
BUILTIN_OBJS += builtin/get-tar-commit-id.o
BUILTIN_OBJS += builtin/grep.o
BUILTIN_OBJS += builtin/hash-object.o
BUILTIN_OBJS += builtin/help.o
BUILTIN_OBJS += builtin/hook.o
BUILTIN_OBJS += builtin/index-pack.o
BUILTIN_OBJS += builtin/init-db.o
BUILTIN_OBJS += builtin/interpret-trailers.o
BUILTIN_OBJS += builtin/log.o
BUILTIN_OBJS += builtin/ls-files.o
BUILTIN_OBJS += builtin/ls-remote.o
BUILTIN_OBJS += builtin/ls-tree.o
BUILTIN_OBJS += builtin/mailinfo.o
BUILTIN_OBJS += builtin/mailsplit.o
BUILTIN_OBJS += builtin/merge-base.o
BUILTIN_OBJS += builtin/merge-file.o
BUILTIN_OBJS += builtin/merge-index.o
BUILTIN_OBJS += builtin/merge-ours.o
BUILTIN_OBJS += builtin/merge-recursive.o
BUILTIN_OBJS += builtin/merge-tree.o
BUILTIN_OBJS += builtin/merge.o
BUILTIN_OBJS += builtin/mktag.o
BUILTIN_OBJS += builtin/mktree.o
BUILTIN_OBJS += builtin/multi-pack-index.o
BUILTIN_OBJS += builtin/mv.o
BUILTIN_OBJS += builtin/name-rev.o
BUILTIN_OBJS += builtin/notes.o
BUILTIN_OBJS += builtin/pack-objects.o
BUILTIN_OBJS += builtin/pack-redundant.o
BUILTIN_OBJS += builtin/pack-refs.o
BUILTIN_OBJS += builtin/patch-id.o
BUILTIN_OBJS += builtin/prune-packed.o
BUILTIN_OBJS += builtin/prune.o
BUILTIN_OBJS += builtin/pull.o
BUILTIN_OBJS += builtin/push.o
BUILTIN_OBJS += builtin/range-diff.o
BUILTIN_OBJS += builtin/read-tree.o
BUILTIN_OBJS += builtin/rebase.o
BUILTIN_OBJS += builtin/receive-pack.o
BUILTIN_OBJS += builtin/reflog.o
BUILTIN_OBJS += builtin/refs.o
BUILTIN_OBJS += builtin/remote-ext.o
BUILTIN_OBJS += builtin/remote-fd.o
BUILTIN_OBJS += builtin/remote.o
BUILTIN_OBJS += builtin/repack.o
BUILTIN_OBJS += builtin/replace.o
BUILTIN_OBJS += builtin/replay.o
BUILTIN_OBJS += builtin/rerere.o
BUILTIN_OBJS += builtin/reset.o
BUILTIN_OBJS += builtin/rev-list.o
BUILTIN_OBJS += builtin/rev-parse.o
BUILTIN_OBJS += builtin/revert.o
BUILTIN_OBJS += builtin/rm.o
BUILTIN_OBJS += builtin/send-pack.o
BUILTIN_OBJS += builtin/shortlog.o
BUILTIN_OBJS += builtin/show-branch.o
BUILTIN_OBJS += builtin/show-index.o
BUILTIN_OBJS += builtin/show-ref.o
BUILTIN_OBJS += builtin/sparse-checkout.o
BUILTIN_OBJS += builtin/stash.o
BUILTIN_OBJS += builtin/stripspace.o
BUILTIN_OBJS += builtin/submodule--helper.o
BUILTIN_OBJS += builtin/symbolic-ref.o
BUILTIN_OBJS += builtin/tag.o
BUILTIN_OBJS += builtin/unpack-file.o
BUILTIN_OBJS += builtin/unpack-objects.o
BUILTIN_OBJS += builtin/update-index.o
BUILTIN_OBJS += builtin/update-ref.o
BUILTIN_OBJS += builtin/update-server-info.o
BUILTIN_OBJS += builtin/upload-archive.o
BUILTIN_OBJS += builtin/upload-pack.o
BUILTIN_OBJS += builtin/var.o
BUILTIN_OBJS += builtin/verify-commit.o
BUILTIN_OBJS += builtin/verify-pack.o
BUILTIN_OBJS += builtin/verify-tag.o
BUILTIN_OBJS += builtin/worktree.o
BUILTIN_OBJS += builtin/write-tree.o

# THIRD_PARTY_SOURCES is a list of patterns compatible with the
# $(filter) and $(filter-out) family of functions. They specify source
# files which are taken from some third-party source where we want to be
# less strict about issues such as coding style so we don't diverge from
# upstream unnecessarily (making merging in future changes easier).
THIRD_PARTY_SOURCES += compat/inet_ntop.c
THIRD_PARTY_SOURCES += compat/inet_pton.c
THIRD_PARTY_SOURCES += compat/nedmalloc/%
THIRD_PARTY_SOURCES += compat/obstack.%
THIRD_PARTY_SOURCES += compat/poll/%
THIRD_PARTY_SOURCES += compat/regex/%
THIRD_PARTY_SOURCES += sha1collisiondetection/%
THIRD_PARTY_SOURCES += sha1dc/%
THIRD_PARTY_SOURCES += $(UNIT_TEST_DIR)/clar/%
THIRD_PARTY_SOURCES += $(UNIT_TEST_DIR)/clar/clar/%

CLAR_TEST_SUITES += ctype
CLAR_TEST_SUITES += strvec
CLAR_TEST_PROG = $(UNIT_TEST_BIN)/unit-tests$(X)
CLAR_TEST_OBJS = $(patsubst %,$(UNIT_TEST_DIR)/%.o,$(CLAR_TEST_SUITES))
CLAR_TEST_OBJS += $(UNIT_TEST_DIR)/clar/clar.o
CLAR_TEST_OBJS += $(UNIT_TEST_DIR)/unit-test.o

UNIT_TEST_PROGRAMS += t-example-decorate
UNIT_TEST_PROGRAMS += t-hash
UNIT_TEST_PROGRAMS += t-hashmap
UNIT_TEST_PROGRAMS += t-mem-pool
UNIT_TEST_PROGRAMS += t-oid-array
UNIT_TEST_PROGRAMS += t-oidmap
UNIT_TEST_PROGRAMS += t-oidtree
UNIT_TEST_PROGRAMS += t-prio-queue
UNIT_TEST_PROGRAMS += t-reftable-basics
UNIT_TEST_PROGRAMS += t-reftable-block
UNIT_TEST_PROGRAMS += t-reftable-merged
UNIT_TEST_PROGRAMS += t-reftable-pq
UNIT_TEST_PROGRAMS += t-reftable-reader
UNIT_TEST_PROGRAMS += t-reftable-readwrite
UNIT_TEST_PROGRAMS += t-reftable-record
UNIT_TEST_PROGRAMS += t-reftable-stack
UNIT_TEST_PROGRAMS += t-reftable-tree
UNIT_TEST_PROGRAMS += t-strbuf
UNIT_TEST_PROGRAMS += t-strcmp-offset
UNIT_TEST_PROGRAMS += t-trailer
UNIT_TEST_PROGRAMS += t-urlmatch-normalization
UNIT_TEST_PROGS = $(patsubst %,$(UNIT_TEST_BIN)/%$X,$(UNIT_TEST_PROGRAMS))
UNIT_TEST_OBJS += $(UNIT_TEST_DIR)/test-lib.o
UNIT_TEST_OBJS += $(UNIT_TEST_DIR)/lib-oid.o
UNIT_TEST_OBJS += $(UNIT_TEST_DIR)/lib-reftable.o

# xdiff and reftable libs may in turn depend on what is in libgit.a
GITLIBS = common-main.o $(LIB_FILE) $(XDIFF_LIB) $(REFTABLE_LIB) $(LIB_FILE)
EXTLIBS =

GIT_USER_AGENT = git/$(GIT_VERSION)

ifeq ($(wildcard sha1collisiondetection/lib/sha1.h),sha1collisiondetection/lib/sha1.h)
DC_SHA1_SUBMODULE = auto
endif

# Set CFLAGS, LDFLAGS and other *FLAGS variables. These might be
# tweaked by config.* below as well as the command-line, both of
# which'll override these defaults.
# Older versions of GCC may require adding "-std=gnu99" at the end.
CFLAGS = -g -O2 -Wall
LDFLAGS =
CC_LD_DYNPATH = -Wl,-rpath,
BASIC_CFLAGS = -I.
BASIC_LDFLAGS =

# library flags
ARFLAGS = rcs
PTHREAD_CFLAGS =

# For the 'sparse' target
SPARSE_FLAGS ?= -std=gnu99
SP_EXTRA_FLAGS =

# For informing GIT-BUILD-OPTIONS of the SANITIZE=leak,address targets
SANITIZE_LEAK =
SANITIZE_ADDRESS =

# For the 'coccicheck' target
SPATCH_INCLUDE_FLAGS = --all-includes
SPATCH_FLAGS =
SPATCH_TEST_FLAGS =

# If *.o files are present, have "coccicheck" depend on them, with
# COMPUTE_HEADER_DEPENDENCIES this will speed up the common-case of
# only needing to re-generate coccicheck results for the users of a
# given API if it's changed, and not all files in the project. If
# COMPUTE_HEADER_DEPENDENCIES=no this will be unset too.
SPATCH_USE_O_DEPENDENCIES = YesPlease

# Set SPATCH_CONCAT_COCCI to concatenate the contrib/cocci/*.cocci
# files into a single contrib/cocci/ALL.cocci before running
# "coccicheck".
#
# Pros:
#
# - Speeds up a one-shot run of "make coccicheck", as we won't have to
#   parse *.[ch] files N times for the N *.cocci rules
#
# Cons:
#
# - Will make incremental development of *.cocci slower, as
#   e.g. changing strbuf.cocci will re-run all *.cocci.
#
# - Makes error and performance analysis harder, as rules will be
#   applied from a monolithic ALL.cocci, rather than
#   e.g. strbuf.cocci. To work around this either undefine this, or
#   generate a specific patch, e.g. this will always use strbuf.cocci,
#   not ALL.cocci:
#
#	make contrib/coccinelle/strbuf.cocci.patch
SPATCH_CONCAT_COCCI = YesPlease

# Rebuild 'coccicheck' if $(SPATCH), its flags etc. change
TRACK_SPATCH_DEFINES =
TRACK_SPATCH_DEFINES += $(SPATCH)
TRACK_SPATCH_DEFINES += $(SPATCH_INCLUDE_FLAGS)
TRACK_SPATCH_DEFINES += $(SPATCH_FLAGS)
TRACK_SPATCH_DEFINES += $(SPATCH_TEST_FLAGS)
GIT-SPATCH-DEFINES: FORCE
	@FLAGS='$(TRACK_SPATCH_DEFINES)'; \
	    if test x"$$FLAGS" != x"`cat GIT-SPATCH-DEFINES 2>/dev/null`" ; then \
		echo >&2 "    * new spatch flags"; \
		echo "$$FLAGS" >GIT-SPATCH-DEFINES; \
            fi

include config.mak.uname
-include config.mak.autogen
-include config.mak

ifdef DEVELOPER
include config.mak.dev
endif

# what 'all' will build and 'install' will install in gitexecdir,
# excluding programs for built-in commands
ALL_PROGRAMS = $(PROGRAMS) $(SCRIPTS)
ALL_COMMANDS_TO_INSTALL = $(ALL_PROGRAMS)
ifeq (,$(SKIP_DASHED_BUILT_INS))
ALL_COMMANDS_TO_INSTALL += $(BUILT_INS)
else
# git-upload-pack, git-receive-pack and git-upload-archive are special: they
# are _expected_ to be present in the `bin/` directory in their dashed form.
ALL_COMMANDS_TO_INSTALL += git-receive-pack$(X)
ALL_COMMANDS_TO_INSTALL += git-upload-archive$(X)
ALL_COMMANDS_TO_INSTALL += git-upload-pack$(X)
endif

ALL_CFLAGS = $(DEVELOPER_CFLAGS) $(CPPFLAGS) $(CFLAGS) $(CFLAGS_APPEND)
ALL_LDFLAGS = $(LDFLAGS) $(LDFLAGS_APPEND)

ifdef SANITIZE
SANITIZERS := $(foreach flag,$(subst $(comma),$(space),$(SANITIZE)),$(flag))
BASIC_CFLAGS += -fsanitize=$(SANITIZE) -fno-sanitize-recover=$(SANITIZE)
BASIC_CFLAGS += -fno-omit-frame-pointer
ifneq ($(filter undefined,$(SANITIZERS)),)
BASIC_CFLAGS += -DSHA1DC_FORCE_ALIGNED_ACCESS
endif
ifneq ($(filter leak,$(SANITIZERS)),)
BASIC_CFLAGS += -DSUPPRESS_ANNOTATED_LEAKS
BASIC_CFLAGS += -O0
SANITIZE_LEAK = YesCompiledWithIt
endif
ifneq ($(filter address,$(SANITIZERS)),)
NO_REGEX = NeededForASAN
SANITIZE_ADDRESS = YesCompiledWithIt
endif
endif

ifndef sysconfdir
ifeq ($(prefix),/usr)
sysconfdir = /etc
else
sysconfdir = etc
endif
endif

ifndef COMPUTE_HEADER_DEPENDENCIES
COMPUTE_HEADER_DEPENDENCIES = auto
endif

ifeq ($(COMPUTE_HEADER_DEPENDENCIES),auto)
dep_check = $(shell $(CC) $(ALL_CFLAGS) \
	-Wno-pedantic \
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

ifndef GENERATE_COMPILATION_DATABASE
GENERATE_COMPILATION_DATABASE = no
endif

ifeq ($(GENERATE_COMPILATION_DATABASE),yes)
compdb_check = $(shell $(CC) $(ALL_CFLAGS) \
	-Wno-pedantic \
	-c -MJ /dev/null \
	-x c /dev/null -o /dev/null 2>&1; \
	echo $$?)
ifneq ($(compdb_check),0)
override GENERATE_COMPILATION_DATABASE = no
$(warning GENERATE_COMPILATION_DATABASE is set to "yes", but your compiler does not \
support generating compilation database entries)
endif
else
ifneq ($(GENERATE_COMPILATION_DATABASE),no)
$(error please set GENERATE_COMPILATION_DATABASE to "yes" or "no" \
(not "$(GENERATE_COMPILATION_DATABASE)"))
endif
endif

ifdef SANE_TOOL_PATH
SANE_TOOL_PATH_SQ = $(subst ','\'',$(SANE_TOOL_PATH))
BROKEN_PATH_FIX = 's|^\# @@BROKEN_PATH_FIX@@$$|git_broken_path_fix "$(SANE_TOOL_PATH_SQ)"|'
PATH := $(SANE_TOOL_PATH):${PATH}
else
BROKEN_PATH_FIX = '/^\# @@BROKEN_PATH_FIX@@$$/d'
endif

ifeq (,$(HOST_CPU))
	BASIC_CFLAGS += -DGIT_HOST_CPU="\"$(firstword $(subst -, ,$(uname_M)))\""
else
	BASIC_CFLAGS += -DGIT_HOST_CPU="\"$(HOST_CPU)\""
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
		NO_OPENSSL = YesPlease
		APPLE_COMMON_CRYPTO = YesPlease
		COMPAT_CFLAGS += -DAPPLE_COMMON_CRYPTO
        endif
	PTHREAD_LIBS =
endif

ifdef NO_LIBGEN_H
	COMPAT_CFLAGS += -DNO_LIBGEN_H
	COMPAT_OBJS += compat/basename.o
endif

ifdef USE_LIBPCRE1
$(error The USE_LIBPCRE1 build option has been removed, use version 2 with USE_LIBPCRE)
endif

USE_LIBPCRE2 ?= $(USE_LIBPCRE)

ifneq (,$(USE_LIBPCRE2))
	BASIC_CFLAGS += -DUSE_LIBPCRE2
	EXTLIBS += -lpcre2-8
endif

ifdef LIBPCREDIR
	BASIC_CFLAGS += -I$(LIBPCREDIR)/include
	EXTLIBS += $(call libpath_template,$(LIBPCREDIR)/$(lib))
endif

ifdef HAVE_ALLOCA_H
	BASIC_CFLAGS += -DHAVE_ALLOCA_H
endif

IMAP_SEND_BUILDDEPS =
IMAP_SEND_LDFLAGS =

ifdef NO_CURL
	BASIC_CFLAGS += -DNO_CURL
	REMOTE_CURL_PRIMARY =
	REMOTE_CURL_ALIASES =
	REMOTE_CURL_NAMES =
	EXCLUDED_PROGRAMS += git-http-fetch git-http-push
else
        ifdef CURLDIR
		# Try "-Wl,-rpath=$(CURLDIR)/$(lib)" in such a case.
		CURL_CFLAGS = -I$(CURLDIR)/include
		CURL_LIBCURL = $(call libpath_template,$(CURLDIR)/$(lib))
        else
		CURL_CFLAGS =
		CURL_LIBCURL =
        endif

        ifndef CURL_LDFLAGS
		CURL_LDFLAGS = $(eval CURL_LDFLAGS := $$(shell $$(CURL_CONFIG) --libs))$(CURL_LDFLAGS)
        endif
	CURL_LIBCURL += $(CURL_LDFLAGS)

        ifndef CURL_CFLAGS
		CURL_CFLAGS = $(eval CURL_CFLAGS := $$(shell $$(CURL_CONFIG) --cflags))$(CURL_CFLAGS)
        endif
	BASIC_CFLAGS += $(CURL_CFLAGS)

	REMOTE_CURL_PRIMARY = git-remote-http$X
	REMOTE_CURL_ALIASES = git-remote-https$X git-remote-ftp$X git-remote-ftps$X
	REMOTE_CURL_NAMES = $(REMOTE_CURL_PRIMARY) $(REMOTE_CURL_ALIASES)
	PROGRAM_OBJS += http-fetch.o
	PROGRAMS += $(REMOTE_CURL_NAMES)
        ifndef NO_EXPAT
		PROGRAM_OBJS += http-push.o
        endif
	curl_check := $(shell (echo 072200; $(CURL_CONFIG) --vernum | sed -e '/^70[BC]/s/^/0/') 2>/dev/null | sort -r | sed -ne 2p)
        ifeq "$(curl_check)" "072200"
		USE_CURL_FOR_IMAP_SEND = YesPlease
        endif
        ifdef USE_CURL_FOR_IMAP_SEND
		BASIC_CFLAGS += -DUSE_CURL_FOR_IMAP_SEND
		IMAP_SEND_BUILDDEPS = http.o
		IMAP_SEND_LDFLAGS += $(CURL_LIBCURL)
        endif
        ifndef NO_EXPAT
                ifdef EXPATDIR
			BASIC_CFLAGS += -I$(EXPATDIR)/include
			EXPAT_LIBEXPAT = $(call libpath_template,$(EXPATDIR)/$(lib)) -lexpat
                else
			EXPAT_LIBEXPAT = -lexpat
                endif
                ifdef EXPAT_NEEDS_XMLPARSE_H
			BASIC_CFLAGS += -DEXPAT_NEEDS_XMLPARSE_H
                endif
        endif
endif
IMAP_SEND_LDFLAGS += $(OPENSSL_LINK) $(OPENSSL_LIBSSL) $(LIB_4_CRYPTO)

ifdef ZLIB_PATH
	BASIC_CFLAGS += -I$(ZLIB_PATH)/include
	EXTLIBS += $(call libpath_template,$(ZLIB_PATH)/$(lib))
endif
EXTLIBS += -lz

ifndef NO_OPENSSL
	OPENSSL_LIBSSL = -lssl
        ifdef OPENSSLDIR
		BASIC_CFLAGS += -I$(OPENSSLDIR)/include
		OPENSSL_LINK = $(call libpath_template,$(OPENSSLDIR)/$(lib))
        else
		OPENSSL_LINK =
        endif
        ifdef NEEDS_CRYPTO_WITH_SSL
		OPENSSL_LIBSSL += -lcrypto
        endif
else
	BASIC_CFLAGS += -DNO_OPENSSL
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
ifndef NO_ICONV
        ifdef NEEDS_LIBICONV
                ifdef ICONVDIR
			BASIC_CFLAGS += -I$(ICONVDIR)/include
			ICONV_LINK = $(call libpath_template,$(ICONVDIR)/$(lib))
                else
			ICONV_LINK =
                endif
                ifdef NEEDS_LIBINTL_BEFORE_LIBICONV
			ICONV_LINK += -lintl
                endif
		EXTLIBS += $(ICONV_LINK) -liconv
        endif
endif
ifdef ICONV_OMITS_BOM
	BASIC_CFLAGS += -DICONV_OMITS_BOM
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
ifdef OPEN_RETURNS_EINTR
	COMPAT_CFLAGS += -DOPEN_RETURNS_EINTR
	COMPAT_OBJS += compat/open.o
endif
ifdef NO_SYMLINK_HEAD
	BASIC_CFLAGS += -DNO_SYMLINK_HEAD
endif
ifdef NO_GETTEXT
	BASIC_CFLAGS += -DNO_GETTEXT
	USE_GETTEXT_SCHEME ?= fallthrough
endif
ifdef NO_POLL
	NO_POLL_H = YesPlease
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
ifdef NO_UNSETENV
	COMPAT_CFLAGS += -DNO_UNSETENV
	COMPAT_OBJS += compat/unsetenv.o
endif
ifdef NO_SYS_SELECT_H
	BASIC_CFLAGS += -DNO_SYS_SELECT_H
endif
ifdef NO_POLL_H
	BASIC_CFLAGS += -DNO_POLL_H
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
ifdef MMAP_PREVENTS_DELETE
	BASIC_CFLAGS += -DMMAP_PREVENTS_DELETE
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
ifdef HAVE_WRITEV
	COMPAT_CFLAGS += -DHAVE_WRITEV
endif
ifdef NO_FAST_WORKING_DIRECTORY
	BASIC_CFLAGS += -DNO_FAST_WORKING_DIRECTORY
endif
ifdef NO_TRUSTABLE_FILEMODE
	BASIC_CFLAGS += -DNO_TRUSTABLE_FILEMODE
endif
ifdef NEEDS_MODE_TRANSLATION
	COMPAT_CFLAGS += -DNEEDS_MODE_TRANSLATION
	COMPAT_OBJS += compat/stat.o
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
ifdef NO_UNIX_SOCKETS
	BASIC_CFLAGS += -DNO_UNIX_SOCKETS
else
	LIB_OBJS += unix-socket.o
	LIB_OBJS += unix-stream-server.o
endif

# Simple IPC requires threads and platform-specific IPC support.
# Only platforms that have both should include these source files
# in the build.
#
# On Windows-based systems, Simple IPC requires threads and Windows
# Named Pipes.  These are always available, so Simple IPC support
# is optional.
#
# On Unix-based systems, Simple IPC requires pthreads and Unix
# domain sockets.  So support is only enabled when both are present.
#
ifdef USE_WIN32_IPC
	BASIC_CFLAGS += -DSUPPORTS_SIMPLE_IPC
	LIB_OBJS += compat/simple-ipc/ipc-shared.o
	LIB_OBJS += compat/simple-ipc/ipc-win32.o
else
ifndef NO_PTHREADS
ifndef NO_UNIX_SOCKETS
	BASIC_CFLAGS += -DSUPPORTS_SIMPLE_IPC
	LIB_OBJS += compat/simple-ipc/ipc-shared.o
	LIB_OBJS += compat/simple-ipc/ipc-unix-socket.o
endif
endif
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

ifdef APPLE_COMMON_CRYPTO_SHA1
	# Apple CommonCrypto requires chunking
	SHA1_MAX_BLOCK_SIZE = 1024L*1024L*1024L
endif

ifdef PPC_SHA1
$(error the PPC_SHA1 flag has been removed along with the PowerPC-specific SHA-1 implementation.)
endif

ifdef OPENSSL_SHA1
	EXTLIBS += $(LIB_4_CRYPTO)
	BASIC_CFLAGS += -DSHA1_OPENSSL
else
ifdef BLK_SHA1
	LIB_OBJS += block-sha1/sha1.o
	BASIC_CFLAGS += -DSHA1_BLK
else
ifdef APPLE_COMMON_CRYPTO_SHA1
	COMPAT_CFLAGS += -DCOMMON_DIGEST_FOR_OPENSSL
	BASIC_CFLAGS += -DSHA1_APPLE
else
	BASIC_CFLAGS += -DSHA1_DC
	LIB_OBJS += sha1dc_git.o
ifdef DC_SHA1_EXTERNAL
        ifdef DC_SHA1_SUBMODULE
                ifneq ($(DC_SHA1_SUBMODULE),auto)
$(error Only set DC_SHA1_EXTERNAL or DC_SHA1_SUBMODULE, not both)
                endif
        endif
	BASIC_CFLAGS += -DDC_SHA1_EXTERNAL
	EXTLIBS += -lsha1detectcoll
else
ifdef DC_SHA1_SUBMODULE
	LIB_OBJS += sha1collisiondetection/lib/sha1.o
	LIB_OBJS += sha1collisiondetection/lib/ubc_check.o
	BASIC_CFLAGS += -DDC_SHA1_SUBMODULE
else
	LIB_OBJS += sha1dc/sha1.o
	LIB_OBJS += sha1dc/ubc_check.o
endif
	BASIC_CFLAGS += \
		-DSHA1DC_NO_STANDARD_INCLUDES \
		-DSHA1DC_INIT_SAFE_HASH_DEFAULT=0 \
		-DSHA1DC_CUSTOM_INCLUDE_SHA1_C="\"git-compat-util.h\"" \
		-DSHA1DC_CUSTOM_INCLUDE_UBC_CHECK_C="\"git-compat-util.h\""
endif
endif
endif
endif

ifdef OPENSSL_SHA1_UNSAFE
ifndef OPENSSL_SHA1
	EXTLIBS += $(LIB_4_CRYPTO)
	BASIC_CFLAGS += -DSHA1_OPENSSL_UNSAFE
endif
else
ifdef BLK_SHA1_UNSAFE
ifndef BLK_SHA1
	LIB_OBJS += block-sha1/sha1.o
	BASIC_CFLAGS += -DSHA1_BLK_UNSAFE
endif
else
ifdef APPLE_COMMON_CRYPTO_SHA1_UNSAFE
ifndef APPLE_COMMON_CRYPTO_SHA1
	COMPAT_CFLAGS += -DCOMMON_DIGEST_FOR_OPENSSL
	BASIC_CFLAGS += -DSHA1_APPLE_UNSAFE
endif
endif
endif
endif

ifdef OPENSSL_SHA256
	EXTLIBS += $(LIB_4_CRYPTO)
	BASIC_CFLAGS += -DSHA256_OPENSSL
else
ifdef NETTLE_SHA256
	BASIC_CFLAGS += -DSHA256_NETTLE
	EXTLIBS += -lnettle
else
ifdef GCRYPT_SHA256
	BASIC_CFLAGS += -DSHA256_GCRYPT
	EXTLIBS += -lgcrypt
else
	LIB_OBJS += sha256/block/sha256.o
	BASIC_CFLAGS += -DSHA256_BLK
endif
endif
endif

ifdef SHA1_MAX_BLOCK_SIZE
	LIB_OBJS += compat/sha1-chunked.o
	BASIC_CFLAGS += -DSHA1_MAX_BLOCK_SIZE="$(SHA1_MAX_BLOCK_SIZE)"
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
endif
ifdef HAVE_ISO_QSORT_S
	COMPAT_CFLAGS += -DHAVE_ISO_QSORT_S
else
	COMPAT_OBJS += compat/qsort_s.o
endif
ifdef RUNTIME_PREFIX
	COMPAT_CFLAGS += -DRUNTIME_PREFIX
endif

ifdef NO_PTHREADS
	BASIC_CFLAGS += -DNO_PTHREADS
else
	BASIC_CFLAGS += $(PTHREAD_CFLAGS)
	EXTLIBS += $(PTHREAD_LIBS)
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
else
ifdef USE_ENHANCED_BASIC_REGULAR_EXPRESSIONS
	COMPAT_CFLAGS += -DUSE_ENHANCED_BASIC_REGULAR_EXPRESSIONS
	COMPAT_OBJS += compat/regcomp_enhanced.o
endif
endif
ifdef NATIVE_CRLF
	BASIC_CFLAGS += -DNATIVE_CRLF
endif

ifdef USE_NED_ALLOCATOR
	COMPAT_CFLAGS += -Icompat/nedmalloc
	COMPAT_OBJS += compat/nedmalloc/nedmalloc.o
	OVERRIDE_STRDUP = YesPlease
endif

ifdef OVERRIDE_STRDUP
	COMPAT_CFLAGS += -DOVERRIDE_STRDUP
	COMPAT_OBJS += compat/strdup.o
endif

ifdef GIT_TEST_CMP_USE_COPIED_CONTEXT
	export GIT_TEST_CMP_USE_COPIED_CONTEXT
endif

ifndef NO_MSGFMT_EXTENDED_OPTIONS
	MSGFMT += --check
endif

ifdef HAVE_CLOCK_GETTIME
	BASIC_CFLAGS += -DHAVE_CLOCK_GETTIME
endif

ifdef HAVE_CLOCK_MONOTONIC
	BASIC_CFLAGS += -DHAVE_CLOCK_MONOTONIC
endif

ifdef HAVE_SYNC_FILE_RANGE
	BASIC_CFLAGS += -DHAVE_SYNC_FILE_RANGE
endif

ifdef NEEDS_LIBRT
	EXTLIBS += -lrt
endif

ifdef HAVE_BSD_SYSCTL
	BASIC_CFLAGS += -DHAVE_BSD_SYSCTL
endif

ifdef HAVE_BSD_KERN_PROC_SYSCTL
	BASIC_CFLAGS += -DHAVE_BSD_KERN_PROC_SYSCTL
endif

ifdef HAVE_GETDELIM
	BASIC_CFLAGS += -DHAVE_GETDELIM
endif

ifneq ($(findstring arc4random,$(CSPRNG_METHOD)),)
	BASIC_CFLAGS += -DHAVE_ARC4RANDOM
endif

ifneq ($(findstring libbsd,$(CSPRNG_METHOD)),)
	BASIC_CFLAGS += -DHAVE_ARC4RANDOM_LIBBSD
	EXTLIBS += -lbsd
endif

ifneq ($(findstring getrandom,$(CSPRNG_METHOD)),)
	BASIC_CFLAGS += -DHAVE_GETRANDOM
endif

ifneq ($(findstring getentropy,$(CSPRNG_METHOD)),)
	BASIC_CFLAGS += -DHAVE_GETENTROPY
endif

ifneq ($(findstring rtlgenrandom,$(CSPRNG_METHOD)),)
	BASIC_CFLAGS += -DHAVE_RTLGENRANDOM
endif

ifneq ($(findstring openssl,$(CSPRNG_METHOD)),)
	BASIC_CFLAGS += -DHAVE_OPENSSL_CSPRNG
	EXTLIBS += -lcrypto -lssl
endif

ifneq ($(PROCFS_EXECUTABLE_PATH),)
	procfs_executable_path_SQ = $(subst ','\'',$(PROCFS_EXECUTABLE_PATH))
	BASIC_CFLAGS += '-DPROCFS_EXECUTABLE_PATH="$(procfs_executable_path_SQ)"'
endif

ifndef HAVE_PLATFORM_PROCINFO
	COMPAT_OBJS += compat/stub/procinfo.o
endif

ifdef HAVE_NS_GET_EXECUTABLE_PATH
	BASIC_CFLAGS += -DHAVE_NS_GET_EXECUTABLE_PATH
endif

ifdef HAVE_ZOS_GET_EXECUTABLE_PATH
        BASIC_CFLAGS += -DHAVE_ZOS_GET_EXECUTABLE_PATH
endif

ifdef HAVE_WPGMPTR
	BASIC_CFLAGS += -DHAVE_WPGMPTR
endif

ifdef FILENO_IS_A_MACRO
	COMPAT_CFLAGS += -DFILENO_IS_A_MACRO
	COMPAT_OBJS += compat/fileno.o
endif

ifdef NEED_ACCESS_ROOT_HANDLER
	COMPAT_CFLAGS += -DNEED_ACCESS_ROOT_HANDLER
	COMPAT_OBJS += compat/access.o
endif

ifdef FSMONITOR_DAEMON_BACKEND
	COMPAT_CFLAGS += -DHAVE_FSMONITOR_DAEMON_BACKEND
	COMPAT_OBJS += compat/fsmonitor/fsm-listen-$(FSMONITOR_DAEMON_BACKEND).o
	COMPAT_OBJS += compat/fsmonitor/fsm-health-$(FSMONITOR_DAEMON_BACKEND).o
	COMPAT_OBJS += compat/fsmonitor/fsm-ipc-$(FSMONITOR_DAEMON_BACKEND).o
endif

ifdef FSMONITOR_OS_SETTINGS
	COMPAT_CFLAGS += -DHAVE_FSMONITOR_OS_SETTINGS
	COMPAT_OBJS += compat/fsmonitor/fsm-settings-$(FSMONITOR_OS_SETTINGS).o
	COMPAT_OBJS += compat/fsmonitor/fsm-path-utils-$(FSMONITOR_OS_SETTINGS).o
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

ifndef PAGER_ENV
PAGER_ENV = LESS=FRX LV=-c
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

ETC_GITCONFIG_SQ = $(subst ','\'',$(ETC_GITCONFIG))
ETC_GITATTRIBUTES_SQ = $(subst ','\'',$(ETC_GITATTRIBUTES))

DESTDIR_SQ = $(subst ','\'',$(DESTDIR))
NO_GETTEXT_SQ = $(subst ','\'',$(NO_GETTEXT))
bindir_SQ = $(subst ','\'',$(bindir))
bindir_relative_SQ = $(subst ','\'',$(bindir_relative))
mandir_SQ = $(subst ','\'',$(mandir))
mandir_relative_SQ = $(subst ','\'',$(mandir_relative))
infodir_relative_SQ = $(subst ','\'',$(infodir_relative))
perllibdir_SQ = $(subst ','\'',$(perllibdir))
localedir_SQ = $(subst ','\'',$(localedir))
localedir_relative_SQ = $(subst ','\'',$(localedir_relative))
gitexecdir_SQ = $(subst ','\'',$(gitexecdir))
gitexecdir_relative_SQ = $(subst ','\'',$(gitexecdir_relative))
template_dir_SQ = $(subst ','\'',$(template_dir))
htmldir_relative_SQ = $(subst ','\'',$(htmldir_relative))
prefix_SQ = $(subst ','\'',$(prefix))
perllibdir_relative_SQ = $(subst ','\'',$(perllibdir_relative))
gitwebdir_SQ = $(subst ','\'',$(gitwebdir))
gitwebstaticdir_SQ = $(subst ','\'',$(gitwebstaticdir))

SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))
TEST_SHELL_PATH_SQ = $(subst ','\'',$(TEST_SHELL_PATH))
PERL_PATH_SQ = $(subst ','\'',$(PERL_PATH))
PYTHON_PATH_SQ = $(subst ','\'',$(PYTHON_PATH))
TCLTK_PATH_SQ = $(subst ','\'',$(TCLTK_PATH))
DIFF_SQ = $(subst ','\'',$(DIFF))
PERLLIB_EXTRA_SQ = $(subst ','\'',$(PERLLIB_EXTRA))

# RUNTIME_PREFIX's resolution logic requires resource paths to be expressed
# relative to each other and share an installation path.
#
# This is a dependency in:
# - Git's binary RUNTIME_PREFIX logic in (see "exec_cmd.c").
# - The runtime prefix Perl header (see
#   "perl/header_templates/runtime_prefix.template.pl").
ifdef RUNTIME_PREFIX

ifneq ($(filter /%,$(firstword $(gitexecdir_relative))),)
$(error RUNTIME_PREFIX requires a relative gitexecdir, not: $(gitexecdir))
endif

ifneq ($(filter /%,$(firstword $(localedir_relative))),)
$(error RUNTIME_PREFIX requires a relative localedir, not: $(localedir))
endif

ifndef NO_PERL
ifneq ($(filter /%,$(firstword $(perllibdir_relative))),)
$(error RUNTIME_PREFIX requires a relative perllibdir, not: $(perllibdir))
endif
endif

endif

# We must filter out any object files from $(GITLIBS),
# as it is typically used like:
#
#   foo: foo.o $(GITLIBS)
#	$(CC) $(filter %.o,$^) $(LIBS)
#
# where we use it as a dependency. Since we also pull object files
# from the dependency list, that would make each entry appear twice.
LIBS = $(filter-out %.o, $(GITLIBS)) $(EXTLIBS)

BASIC_CFLAGS += $(COMPAT_CFLAGS)
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
	@if test -n "$$GIT_PERF_REPO" || test -d .git; then \
		$(MAKE) PROFILE=GEN -j1 perf; \
	else \
		echo "Skipping profile of perf tests..."; \
	fi
	$(MAKE) PROFILE=USE all

profile-fast: profile-clean
	$(MAKE) PROFILE=GEN all
	$(MAKE) PROFILE=GEN -j1 perf
	$(MAKE) PROFILE=USE all


all:: $(ALL_COMMANDS_TO_INSTALL) $(SCRIPT_LIB) $(OTHER_PROGRAMS) GIT-BUILD-OPTIONS
ifneq (,$X)
	$(QUIET_BUILT_IN)$(foreach p,$(patsubst %$X,%,$(filter %$X,$(ALL_COMMANDS_TO_INSTALL) $(OTHER_PROGRAMS))), if test ! -d '$p' && test ! '$p' -ef '$p$X'; then $(RM) '$p'; fi;)
endif

all::
ifndef NO_TCLTK
	$(QUIET_SUBDIR0)git-gui $(QUIET_SUBDIR1) gitexecdir='$(gitexec_instdir_SQ)' all
	$(QUIET_SUBDIR0)gitk-git $(QUIET_SUBDIR1) all
endif
	$(QUIET_SUBDIR0)templates $(QUIET_SUBDIR1) SHELL_PATH='$(SHELL_PATH_SQ)' PERL_PATH='$(PERL_PATH_SQ)'

# If you add a new fuzzer, please also make sure to run it in
# ci/run-build-and-minimal-fuzzers.sh so that we make sure it still links and
# runs in the future.
FUZZ_OBJS += oss-fuzz/dummy-cmd-main.o
FUZZ_OBJS += oss-fuzz/fuzz-commit-graph.o
FUZZ_OBJS += oss-fuzz/fuzz-config.o
FUZZ_OBJS += oss-fuzz/fuzz-date.o
FUZZ_OBJS += oss-fuzz/fuzz-pack-headers.o
FUZZ_OBJS += oss-fuzz/fuzz-pack-idx.o
.PHONY: fuzz-objs
fuzz-objs: $(FUZZ_OBJS)

# Always build fuzz objects even if not testing, to prevent bit-rot.
all:: $(FUZZ_OBJS)

FUZZ_PROGRAMS += $(patsubst %.o,%,$(filter-out %dummy-cmd-main.o,$(FUZZ_OBJS)))

# Build fuzz programs when possible, even without the necessary fuzzing support,
# to prevent bit-rot.
ifdef LINK_FUZZ_PROGRAMS
all:: $(FUZZ_PROGRAMS)
endif

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
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) \
		$(filter %.o,$^) $(LIBS)

help.sp help.s help.o: command-list.h
builtin/bugreport.sp builtin/bugreport.s builtin/bugreport.o: hook-list.h

builtin/help.sp builtin/help.s builtin/help.o: config-list.h GIT-PREFIX
builtin/help.sp builtin/help.s builtin/help.o: EXTRA_CPPFLAGS = \
	'-DGIT_HTML_PATH="$(htmldir_relative_SQ)"' \
	'-DGIT_MAN_PATH="$(mandir_relative_SQ)"' \
	'-DGIT_INFO_PATH="$(infodir_relative_SQ)"'

PAGER_ENV_SQ = $(subst ','\'',$(PAGER_ENV))
PAGER_ENV_CQ = "$(subst ",\",$(subst \,\\,$(PAGER_ENV)))"
PAGER_ENV_CQ_SQ = $(subst ','\'',$(PAGER_ENV_CQ))
pager.sp pager.s pager.o: EXTRA_CPPFLAGS = \
	-DPAGER_ENV='$(PAGER_ENV_CQ_SQ)'

version.sp version.s version.o: GIT-VERSION-FILE GIT-USER-AGENT
version.sp version.s version.o: EXTRA_CPPFLAGS = \
	'-DGIT_VERSION="$(GIT_VERSION)"' \
	'-DGIT_USER_AGENT=$(GIT_USER_AGENT_CQ_SQ)' \
	'-DGIT_BUILT_FROM_COMMIT="$(shell \
		GIT_CEILING_DIRECTORIES="$(CURDIR)/.." \
		git rev-parse -q --verify HEAD 2>/dev/null)"'

$(BUILT_INS): git$X
	$(QUIET_BUILT_IN)$(RM) $@ && \
	ln $< $@ 2>/dev/null || \
	ln -s $< $@ 2>/dev/null || \
	cp $< $@

config-list.h: generate-configlist.sh

config-list.h: Documentation/*config.txt Documentation/config/*.txt
	$(QUIET_GEN)$(SHELL_PATH) ./generate-configlist.sh >$@

command-list.h: generate-cmdlist.sh command-list.txt

command-list.h: $(wildcard Documentation/git*.txt)
	$(QUIET_GEN)$(SHELL_PATH) ./generate-cmdlist.sh \
		$(patsubst %,--exclude-program %,$(EXCLUDED_PROGRAMS)) \
		command-list.txt >$@

hook-list.h: generate-hooklist.sh Documentation/githooks.txt
	$(QUIET_GEN)$(SHELL_PATH) ./generate-hooklist.sh >$@

SCRIPT_DEFINES = $(SHELL_PATH_SQ):$(DIFF_SQ):\
	$(localedir_SQ):$(USE_GETTEXT_SCHEME):$(SANE_TOOL_PATH_SQ):\
	$(gitwebdir_SQ):$(PERL_PATH_SQ):$(PAGER_ENV):\
	$(perllibdir_SQ)
GIT-SCRIPT-DEFINES: FORCE
	@FLAGS='$(SCRIPT_DEFINES)'; \
	    if test x"$$FLAGS" != x"`cat $@ 2>/dev/null`" ; then \
		echo >&2 "    * new script parameters"; \
		echo "$$FLAGS" >$@; \
            fi

define cmd_munge_script
sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
    -e 's|@SHELL_PATH@|$(SHELL_PATH_SQ)|' \
    -e 's|@@DIFF@@|$(DIFF_SQ)|' \
    -e 's|@@LOCALEDIR@@|$(localedir_SQ)|g' \
    -e 's/@@USE_GETTEXT_SCHEME@@/$(USE_GETTEXT_SCHEME)/g' \
    -e $(BROKEN_PATH_FIX) \
    -e 's|@@GITWEBDIR@@|$(gitwebdir_SQ)|g' \
    -e 's|@@PERL@@|$(PERL_PATH_SQ)|g' \
    -e 's|@@PAGER_ENV@@|$(PAGER_ENV_SQ)|g' \
    $@.sh >$@+
endef

$(SCRIPT_SH_GEN) : % : %.sh GIT-SCRIPT-DEFINES
	$(QUIET_GEN)$(cmd_munge_script) && \
	chmod +x $@+ && \
	mv $@+ $@

$(SCRIPT_LIB) : % : %.sh GIT-SCRIPT-DEFINES
	$(QUIET_GEN)$(cmd_munge_script) && \
	mv $@+ $@

git.res: git.rc GIT-VERSION-FILE GIT-PREFIX
	$(QUIET_RC)$(RC) \
	  $(join -DMAJOR= -DMINOR= -DMICRO= -DPATCHLEVEL=, $(wordlist 1, 4, \
	    $(shell echo $(GIT_VERSION) 0 0 0 0 | tr '.a-zA-Z-' ' '))) \
	  -DGIT_VERSION="\\\"$(GIT_VERSION)\\\"" -i $< -o $@

# This makes sure we depend on the NO_PERL setting itself.
$(SCRIPT_PERL_GEN): GIT-BUILD-OPTIONS

# Used for substitution in Perl modules. Disabled when using RUNTIME_PREFIX
# since the locale directory is injected.
perl_localedir_SQ = $(localedir_SQ)

ifndef NO_PERL
PERL_HEADER_TEMPLATE = perl/header_templates/fixed_prefix.template.pl
PERL_DEFINES =
PERL_DEFINES += $(PERL_PATH_SQ)
PERL_DEFINES += $(PERLLIB_EXTRA_SQ)
PERL_DEFINES += $(perllibdir_SQ)
PERL_DEFINES += $(RUNTIME_PREFIX)
PERL_DEFINES += $(NO_PERL_CPAN_FALLBACKS)
PERL_DEFINES += $(NO_GETTEXT)

# Support Perl runtime prefix. In this mode, a different header is installed
# into Perl scripts.
ifdef RUNTIME_PREFIX

PERL_HEADER_TEMPLATE = perl/header_templates/runtime_prefix.template.pl

# Don't export a fixed $(localedir) path; it will be resolved by the Perl header
# at runtime.
perl_localedir_SQ =

endif

PERL_DEFINES += $(gitexecdir) $(perllibdir) $(localedir)

$(SCRIPT_PERL_GEN): % : %.perl GIT-PERL-DEFINES GIT-PERL-HEADER GIT-VERSION-FILE
	$(QUIET_GEN) \
	sed -e '1{' \
	    -e '	s|#!.*perl|#!$(PERL_PATH_SQ)|' \
	    -e '	r GIT-PERL-HEADER' \
	    -e '	G' \
	    -e '}' \
	    -e 's/@@GIT_VERSION@@/$(GIT_VERSION)/g' \
	    $< >$@+ && \
	chmod +x $@+ && \
	mv $@+ $@

PERL_DEFINES := $(subst $(space),:,$(PERL_DEFINES))
GIT-PERL-DEFINES: FORCE
	@FLAGS='$(PERL_DEFINES)'; \
	    if test x"$$FLAGS" != x"`cat $@ 2>/dev/null`" ; then \
		echo >&2 "    * new perl-specific parameters"; \
		echo "$$FLAGS" >$@; \
	    fi

GIT-PERL-HEADER: $(PERL_HEADER_TEMPLATE) GIT-PERL-DEFINES Makefile
	$(QUIET_GEN) \
	INSTLIBDIR='$(perllibdir_SQ)' && \
	INSTLIBDIR_EXTRA='$(PERLLIB_EXTRA_SQ)' && \
	INSTLIBDIR="$$INSTLIBDIR$${INSTLIBDIR_EXTRA:+:$$INSTLIBDIR_EXTRA}" && \
	sed -e 's=@@PATHSEP@@=$(pathsep)=g' \
	    -e "s=@@INSTLIBDIR@@=$$INSTLIBDIR=g" \
	    -e 's=@@PERLLIBDIR_REL@@=$(perllibdir_relative_SQ)=g' \
	    -e 's=@@GITEXECDIR_REL@@=$(gitexecdir_relative_SQ)=g' \
	    -e 's=@@LOCALEDIR_REL@@=$(localedir_relative_SQ)=g' \
	    $< >$@+ && \
	mv $@+ $@

.PHONY: perllibdir
perllibdir:
	@echo '$(perllibdir_SQ)'

git-instaweb: git-instaweb.sh GIT-SCRIPT-DEFINES
	$(QUIET_GEN)$(cmd_munge_script) && \
	chmod +x $@+ && \
	mv $@+ $@
else # NO_PERL
$(SCRIPT_PERL_GEN) git-instaweb: % : unimplemented.sh
	$(QUIET_GEN) \
	sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
	    -e 's|@@REASON@@|NO_PERL=$(NO_PERL)|g' \
	    unimplemented.sh >$@+ && \
	chmod +x $@+ && \
	mv $@+ $@
endif # NO_PERL

# This makes sure we depend on the NO_PYTHON setting itself.
$(SCRIPT_PYTHON_GEN): GIT-BUILD-OPTIONS

ifndef NO_PYTHON
$(SCRIPT_PYTHON_GEN): GIT-CFLAGS GIT-PREFIX GIT-PYTHON-VARS
$(SCRIPT_PYTHON_GEN): % : %.py
	$(QUIET_GEN) \
	sed -e '1s|#!.*python|#!$(PYTHON_PATH_SQ)|' \
	    $< >$@+ && \
	chmod +x $@+ && \
	mv $@+ $@
else # NO_PYTHON
$(SCRIPT_PYTHON_GEN): % : unimplemented.sh
	$(QUIET_GEN) \
	sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
	    -e 's|@@REASON@@|NO_PYTHON=$(NO_PYTHON)|g' \
	    unimplemented.sh >$@+ && \
	chmod +x $@+ && \
	mv $@+ $@
endif # NO_PYTHON

CONFIGURE_RECIPE = sed -e 's/@@GIT_VERSION@@/$(GIT_VERSION)/g' \
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
XDIFF_OBJS += xdiff/xemit.o
XDIFF_OBJS += xdiff/xhistogram.o
XDIFF_OBJS += xdiff/xmerge.o
XDIFF_OBJS += xdiff/xpatience.o
XDIFF_OBJS += xdiff/xprepare.o
XDIFF_OBJS += xdiff/xutils.o
.PHONY: xdiff-objs
xdiff-objs: $(XDIFF_OBJS)

REFTABLE_OBJS += reftable/basics.o
REFTABLE_OBJS += reftable/error.o
REFTABLE_OBJS += reftable/block.o
REFTABLE_OBJS += reftable/blocksource.o
REFTABLE_OBJS += reftable/iter.o
REFTABLE_OBJS += reftable/merged.o
REFTABLE_OBJS += reftable/pq.o
REFTABLE_OBJS += reftable/reader.o
REFTABLE_OBJS += reftable/record.o
REFTABLE_OBJS += reftable/stack.o
REFTABLE_OBJS += reftable/system.o
REFTABLE_OBJS += reftable/tree.o
REFTABLE_OBJS += reftable/writer.o

TEST_OBJS := $(patsubst %$X,%.o,$(TEST_PROGRAMS)) $(patsubst %,t/helper/%,$(TEST_BUILTINS_OBJS))

.PHONY: test-objs
test-objs: $(TEST_OBJS)

GIT_OBJS += $(LIB_OBJS)
GIT_OBJS += $(BUILTIN_OBJS)
GIT_OBJS += common-main.o
GIT_OBJS += git.o
.PHONY: git-objs
git-objs: $(GIT_OBJS)

SCALAR_OBJS += scalar.o
.PHONY: scalar-objs
scalar-objs: $(SCALAR_OBJS)

OBJECTS += $(GIT_OBJS)
OBJECTS += $(SCALAR_OBJS)
OBJECTS += $(PROGRAM_OBJS)
OBJECTS += $(TEST_OBJS)
OBJECTS += $(XDIFF_OBJS)
OBJECTS += $(FUZZ_OBJS)
OBJECTS += $(REFTABLE_OBJS) $(REFTABLE_TEST_OBJS)
OBJECTS += $(UNIT_TEST_OBJS)
OBJECTS += $(CLAR_TEST_OBJS)
OBJECTS += $(patsubst %,$(UNIT_TEST_DIR)/%.o,$(UNIT_TEST_PROGRAMS))

ifndef NO_CURL
	OBJECTS += http.o http-walker.o remote-curl.o
endif

.PHONY: objects
objects: $(OBJECTS)

dep_files := $(foreach f,$(OBJECTS),$(dir $f).depend/$(notdir $f).d)
dep_dirs := $(addsuffix .depend,$(sort $(dir $(OBJECTS))))

ifeq ($(COMPUTE_HEADER_DEPENDENCIES),yes)
$(dep_dirs):
	@mkdir -p $@

missing_dep_dirs := $(filter-out $(wildcard $(dep_dirs)),$(dep_dirs))
dep_file = $(dir $@).depend/$(notdir $@).d
dep_args = -MF $(dep_file) -MQ $@ -MMD -MP
endif

ifneq ($(COMPUTE_HEADER_DEPENDENCIES),yes)
missing_dep_dirs =
dep_args =
endif

compdb_dir = compile_commands

ifeq ($(GENERATE_COMPILATION_DATABASE),yes)
missing_compdb_dir = $(compdb_dir)
$(missing_compdb_dir):
	@mkdir -p $@

compdb_file = $(compdb_dir)/$(subst /,-,$@.json)
compdb_args = -MJ $(compdb_file)
else
missing_compdb_dir =
compdb_args =
endif

$(OBJECTS): %.o: %.c GIT-CFLAGS $(missing_dep_dirs) $(missing_compdb_dir)
	$(QUIET_CC)$(CC) -o $*.o -c $(dep_args) $(compdb_args) $(ALL_CFLAGS) $(EXTRA_CPPFLAGS) $<

%.s: %.c GIT-CFLAGS FORCE
	$(QUIET_CC)$(CC) -o $@ -S $(ALL_CFLAGS) $(EXTRA_CPPFLAGS) $<

ifdef USE_COMPUTED_HEADER_DEPENDENCIES
# Take advantage of gcc's on-the-fly dependency generation
# See <https://gcc.gnu.org/gcc-3.0/features.html>.
dep_files_present := $(wildcard $(dep_files))
ifneq ($(dep_files_present),)
include $(dep_files_present)
endif
else
$(OBJECTS): $(LIB_H) $(GENERATED_H)
endif

ifeq ($(GENERATE_COMPILATION_DATABASE),yes)
all:: compile_commands.json
compile_commands.json:
	$(QUIET_GEN)sed -e '1s/^/[/' -e '$$s/,$$/]/' $(compdb_dir)/*.o.json > $@+
	@if test -s $@+; then mv $@+ $@; else $(RM) $@+; fi
endif

exec-cmd.sp exec-cmd.s exec-cmd.o: GIT-PREFIX
exec-cmd.sp exec-cmd.s exec-cmd.o: EXTRA_CPPFLAGS = \
	'-DGIT_EXEC_PATH="$(gitexecdir_SQ)"' \
	'-DGIT_LOCALE_PATH="$(localedir_relative_SQ)"' \
	'-DBINDIR="$(bindir_relative_SQ)"' \
	'-DFALLBACK_RUNTIME_PREFIX="$(prefix_SQ)"'

setup.sp setup.s setup.o: GIT-PREFIX
setup.sp setup.s setup.o: EXTRA_CPPFLAGS = \
	-DDEFAULT_GIT_TEMPLATE_DIR='"$(template_dir_SQ)"'

config.sp config.s config.o: GIT-PREFIX
config.sp config.s config.o: EXTRA_CPPFLAGS = \
	-DETC_GITCONFIG='"$(ETC_GITCONFIG_SQ)"'

attr.sp attr.s attr.o: GIT-PREFIX
attr.sp attr.s attr.o: EXTRA_CPPFLAGS = \
	-DETC_GITATTRIBUTES='"$(ETC_GITATTRIBUTES_SQ)"'

gettext.sp gettext.s gettext.o: GIT-PREFIX
gettext.sp gettext.s gettext.o: EXTRA_CPPFLAGS = \
	-DGIT_LOCALE_PATH='"$(localedir_relative_SQ)"'

http-push.sp http.sp http-walker.sp remote-curl.sp imap-send.sp: SP_EXTRA_FLAGS += \
	-DCURL_DISABLE_TYPECHECK

pack-revindex.sp: SP_EXTRA_FLAGS += -Wno-memcpy-max-count

ifdef NO_EXPAT
http-walker.sp http-walker.s http-walker.o: EXTRA_CPPFLAGS = -DNO_EXPAT
endif

ifdef NO_REGEX
compat/regex/regex.sp compat/regex/regex.o: EXTRA_CPPFLAGS = \
	-DGAWK -DNO_MBSUPPORT
endif

ifdef USE_NED_ALLOCATOR
compat/nedmalloc/nedmalloc.sp compat/nedmalloc/nedmalloc.o: EXTRA_CPPFLAGS = \
	-DNDEBUG -DREPLACE_SYSTEM_ALLOCATOR
compat/nedmalloc/nedmalloc.sp: SP_EXTRA_FLAGS += -Wno-non-pointer-null
endif

headless-git.o: compat/win32/headless.c GIT-CFLAGS
	$(QUIET_CC)$(CC) $(ALL_CFLAGS) $(COMPAT_CFLAGS) \
		-fno-stack-protector -o $@ -c -Wall -Wwrite-strings $<

headless-git$X: headless-git.o git.res GIT-LDFLAGS
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) $(ALL_LDFLAGS) -mwindows -o $@ $< git.res

git-%$X: %.o GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) $(LIBS)

git-imap-send$X: imap-send.o $(IMAP_SEND_BUILDDEPS) GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) \
		$(IMAP_SEND_LDFLAGS) $(LIBS)

git-http-fetch$X: http.o http-walker.o http-fetch.o GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) \
		$(CURL_LIBCURL) $(LIBS)
git-http-push$X: http.o http-push.o GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) \
		$(CURL_LIBCURL) $(EXPAT_LIBEXPAT) $(LIBS)

$(REMOTE_CURL_ALIASES): $(REMOTE_CURL_PRIMARY)
	$(QUIET_LNCP)$(RM) $@ && \
	ln $< $@ 2>/dev/null || \
	ln -s $< $@ 2>/dev/null || \
	cp $< $@

$(REMOTE_CURL_PRIMARY): remote-curl.o http.o http-walker.o GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) \
		$(CURL_LIBCURL) $(EXPAT_LIBEXPAT) $(LIBS)

scalar$X: scalar.o GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) \
		$(filter %.o,$^) $(LIBS)

$(LIB_FILE): $(LIB_OBJS)
	$(QUIET_AR)$(RM) $@ && $(AR) $(ARFLAGS) $@ $^

$(XDIFF_LIB): $(XDIFF_OBJS)
	$(QUIET_AR)$(RM) $@ && $(AR) $(ARFLAGS) $@ $^

$(REFTABLE_LIB): $(REFTABLE_OBJS)
	$(QUIET_AR)$(RM) $@ && $(AR) $(ARFLAGS) $@ $^

export DEFAULT_EDITOR DEFAULT_PAGER

Documentation/GIT-EXCLUDED-PROGRAMS: FORCE
	@EXCLUDED='EXCLUDED_PROGRAMS := $(EXCLUDED_PROGRAMS)'; \
	    if test x"$$EXCLUDED" != \
		x"`cat Documentation/GIT-EXCLUDED-PROGRAMS 2>/dev/null`" ; then \
		echo >&2 "    * new documentation flags"; \
		echo "$$EXCLUDED" >Documentation/GIT-EXCLUDED-PROGRAMS; \
            fi

.PHONY: doc man man-perl html info pdf
doc: man-perl
	$(MAKE) -C Documentation all

man: man-perl
	$(MAKE) -C Documentation man

man-perl: perl/build/man/man3/Git.3pm

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
	--package-name=Git
XGETTEXT_FLAGS_C = $(XGETTEXT_FLAGS) --language=C \
	--keyword=_ --keyword=N_ --keyword="Q_:1,2"
XGETTEXT_FLAGS_SH = $(XGETTEXT_FLAGS) --language=Shell \
	--keyword=gettextln --keyword=eval_gettextln
XGETTEXT_FLAGS_PERL = $(XGETTEXT_FLAGS) --language=Perl \
	--keyword=__ --keyword=N__ --keyword="__n:1,2"
MSGMERGE_FLAGS = --add-location --backup=off --update
LOCALIZED_C = $(sort $(FOUND_C_SOURCES) $(FOUND_H_SOURCES) $(GENERATED_H))
LOCALIZED_SH = $(sort $(SCRIPT_SH) git-sh-setup.sh)
LOCALIZED_PERL = $(sort $(SCRIPT_PERL))

ifdef XGETTEXT_INCLUDE_TESTS
LOCALIZED_C += t/t0200/test.c
LOCALIZED_SH += t/t0200/test.sh
LOCALIZED_PERL += t/t0200/test.perl
endif

## We generate intermediate .build/pot/po/%.po files containing a
## extract of the translations we find in each file in the source
## tree. We will assemble them using msgcat to create the final
## "po/git.pot" file.
LOCALIZED_ALL_GEN_PO =

LOCALIZED_C_GEN_PO = $(LOCALIZED_C:%=.build/pot/po/%.po)
LOCALIZED_ALL_GEN_PO += $(LOCALIZED_C_GEN_PO)

LOCALIZED_SH_GEN_PO = $(LOCALIZED_SH:%=.build/pot/po/%.po)
LOCALIZED_ALL_GEN_PO += $(LOCALIZED_SH_GEN_PO)

LOCALIZED_PERL_GEN_PO = $(LOCALIZED_PERL:%=.build/pot/po/%.po)
LOCALIZED_ALL_GEN_PO += $(LOCALIZED_PERL_GEN_PO)

## Gettext tools cannot work with our own custom PRItime type, so
## we replace PRItime with PRIuMAX.  We need to update this to
## PRIdMAX if we switch to a signed type later.
$(LOCALIZED_C_GEN_PO): .build/pot/po/%.po: %
	$(call mkdir_p_parent_template)
	$(QUIET_XGETTEXT) \
	    if grep -q PRItime $<; then \
		(\
			sed -e 's|PRItime|PRIuMAX|g' <$< \
				>.build/pot/po/$< && \
			cd .build/pot/po && \
			$(XGETTEXT) --omit-header \
				-o $(@:.build/pot/po/%=%) \
				$(XGETTEXT_FLAGS_C) $< && \
			rm $<; \
		); \
	    else \
		$(XGETTEXT) --omit-header \
			-o $@ $(XGETTEXT_FLAGS_C) $<; \
	    fi

$(LOCALIZED_SH_GEN_PO): .build/pot/po/%.po: %
	$(call mkdir_p_parent_template)
	$(QUIET_XGETTEXT)$(XGETTEXT) --omit-header \
		-o$@ $(XGETTEXT_FLAGS_SH) $<

$(LOCALIZED_PERL_GEN_PO): .build/pot/po/%.po: %
	$(call mkdir_p_parent_template)
	$(QUIET_XGETTEXT)$(XGETTEXT) --omit-header \
		-o$@ $(XGETTEXT_FLAGS_PERL) $<

define gen_pot_header
$(XGETTEXT) $(XGETTEXT_FLAGS_C) \
	-o - /dev/null | \
sed -e 's|charset=CHARSET|charset=UTF-8|' \
    -e 's|\(Last-Translator: \)FULL NAME <.*>|\1make by the Makefile|' \
    -e 's|\(Language-Team: \)LANGUAGE <.*>|\1Git Mailing List <git@vger.kernel.org>|' \
    >$@ && \
echo '"Plural-Forms: nplurals=INTEGER; plural=EXPRESSION;\\n"' >>$@
endef

.build/pot/git.header: $(LOCALIZED_ALL_GEN_PO)
	$(call mkdir_p_parent_template)
	$(QUIET_GEN)$(gen_pot_header)

po/git.pot: .build/pot/git.header $(LOCALIZED_ALL_GEN_PO)
	$(QUIET_GEN)$(MSGCAT) $^ >$@

.PHONY: pot
pot: po/git.pot

define check_po_file_envvar
	$(if $(PO_FILE), \
		$(if $(filter po/%.po,$(PO_FILE)), , \
			$(error PO_FILE should match pattern: "po/%.po")), \
		$(error PO_FILE is not defined))
endef

.PHONY: po-update
po-update: po/git.pot
	$(check_po_file_envvar)
	@if test ! -e $(PO_FILE); then \
		echo >&2 "error: $(PO_FILE) does not exist"; \
		echo >&2 'To create an initial po file, use: "make po-init PO_FILE=po/XX.po"'; \
		exit 1; \
	fi
	$(QUIET_MSGMERGE)$(MSGMERGE) $(MSGMERGE_FLAGS) $(PO_FILE) po/git.pot

.PHONY: check-pot
check-pot: $(LOCALIZED_ALL_GEN_PO)

### TODO FIXME: Translating everything in these files is a bad
### heuristic for "core", as we'll translate obscure error() messages
### along with commonly seen i18n messages. A better heuristic would
### be to e.g. use spatch to first remove error/die/warning
### etc. messages.
LOCALIZED_C_CORE =
LOCALIZED_C_CORE += builtin/checkout.c
LOCALIZED_C_CORE += builtin/clone.c
LOCALIZED_C_CORE += builtin/index-pack.c
LOCALIZED_C_CORE += builtin/push.c
LOCALIZED_C_CORE += builtin/reset.c
LOCALIZED_C_CORE += remote.c
LOCALIZED_C_CORE += wt-status.c

LOCALIZED_C_CORE_GEN_PO = $(LOCALIZED_C_CORE:%=.build/pot/po/%.po)

.build/pot/git-core.header: $(LOCALIZED_C_CORE_GEN_PO)
	$(call mkdir_p_parent_template)
	$(QUIET_GEN)$(gen_pot_header)

po/git-core.pot: .build/pot/git-core.header $(LOCALIZED_C_CORE_GEN_PO)
	$(QUIET_GEN)$(MSGCAT) $^ >$@

.PHONY: po-init
po-init: po/git-core.pot
	$(check_po_file_envvar)
	@if test -e $(PO_FILE); then \
		echo >&2 "error: $(PO_FILE) exists already"; \
		exit 1; \
	fi
	$(QUIET_MSGINIT)msginit \
		--input=$< \
		--output=$(PO_FILE) \
		--no-translator \
		--locale=$(PO_FILE:po/%.po=%)

## po/*.po files & their rules
ifdef NO_GETTEXT
POFILES :=
MOFILES :=
else
POFILES := $(wildcard po/*.po)
MOFILES := $(patsubst po/%.po,po/build/locale/%/LC_MESSAGES/git.mo,$(POFILES))

all:: $(MOFILES)
endif

po/build/locale/%/LC_MESSAGES/git.mo: po/%.po
	$(call mkdir_p_parent_template)
	$(QUIET_MSGFMT)$(MSGFMT) -o $@ $<

LIB_PERL := $(wildcard perl/Git.pm perl/Git/*.pm perl/Git/*/*.pm perl/Git/*/*/*.pm)
LIB_PERL_GEN := $(patsubst perl/%.pm,perl/build/lib/%.pm,$(LIB_PERL))
LIB_CPAN := $(wildcard perl/FromCPAN/*.pm perl/FromCPAN/*/*.pm)
LIB_CPAN_GEN := $(patsubst perl/%.pm,perl/build/lib/%.pm,$(LIB_CPAN))

ifndef NO_PERL
all:: $(LIB_PERL_GEN)
ifndef NO_PERL_CPAN_FALLBACKS
all:: $(LIB_CPAN_GEN)
endif
NO_PERL_CPAN_FALLBACKS_SQ = $(subst ','\'',$(NO_PERL_CPAN_FALLBACKS))
endif

perl/build/lib/%.pm: perl/%.pm GIT-PERL-DEFINES
	$(call mkdir_p_parent_template)
	$(QUIET_GEN) \
	sed -e 's|@@LOCALEDIR@@|$(perl_localedir_SQ)|g' \
	    -e 's|@@NO_GETTEXT@@|$(NO_GETTEXT_SQ)|g' \
	    -e 's|@@NO_PERL_CPAN_FALLBACKS@@|$(NO_PERL_CPAN_FALLBACKS_SQ)|g' \
	< $< > $@

perl/build/man/man3/Git.3pm: perl/Git.pm
	$(call mkdir_p_parent_template)
	$(QUIET_GEN)pod2man $< $@

$(ETAGS_TARGET): $(FOUND_SOURCE_FILES)
	$(QUIET_GEN)$(RM) $@+ && \
	echo $(FOUND_SOURCE_FILES) | xargs etags -a -o $@+ && \
	mv $@+ $@

tags: $(FOUND_SOURCE_FILES)
	$(QUIET_GEN)$(RM) $@+ && \
	echo $(FOUND_SOURCE_FILES) | xargs ctags -a -o $@+ && \
	mv $@+ $@

cscope.out: $(FOUND_SOURCE_FILES)
	$(QUIET_GEN)$(RM) $@+ && \
	echo $(FOUND_SOURCE_FILES) | xargs cscope -f$@+ -b && \
	mv $@+ $@

.PHONY: cscope
cscope: cscope.out

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
	@echo SHELL_PATH=\''$(subst ','\'',$(SHELL_PATH_SQ))'\' >$@+
	@echo TEST_SHELL_PATH=\''$(subst ','\'',$(TEST_SHELL_PATH_SQ))'\' >>$@+
	@echo PERL_PATH=\''$(subst ','\'',$(PERL_PATH_SQ))'\' >>$@+
	@echo DIFF=\''$(subst ','\'',$(subst ','\'',$(DIFF)))'\' >>$@+
	@echo PYTHON_PATH=\''$(subst ','\'',$(PYTHON_PATH_SQ))'\' >>$@+
	@echo TAR=\''$(subst ','\'',$(subst ','\'',$(TAR)))'\' >>$@+
	@echo NO_CURL=\''$(subst ','\'',$(subst ','\'',$(NO_CURL)))'\' >>$@+
	@echo NO_ICONV=\''$(subst ','\'',$(subst ','\'',$(NO_ICONV)))'\' >>$@+
	@echo NO_EXPAT=\''$(subst ','\'',$(subst ','\'',$(NO_EXPAT)))'\' >>$@+
	@echo USE_LIBPCRE2=\''$(subst ','\'',$(subst ','\'',$(USE_LIBPCRE2)))'\' >>$@+
	@echo NO_PERL=\''$(subst ','\'',$(subst ','\'',$(NO_PERL)))'\' >>$@+
	@echo NO_PTHREADS=\''$(subst ','\'',$(subst ','\'',$(NO_PTHREADS)))'\' >>$@+
	@echo NO_PYTHON=\''$(subst ','\'',$(subst ','\'',$(NO_PYTHON)))'\' >>$@+
	@echo NO_REGEX=\''$(subst ','\'',$(subst ','\'',$(NO_REGEX)))'\' >>$@+
	@echo NO_UNIX_SOCKETS=\''$(subst ','\'',$(subst ','\'',$(NO_UNIX_SOCKETS)))'\' >>$@+
	@echo PAGER_ENV=\''$(subst ','\'',$(subst ','\'',$(PAGER_ENV)))'\' >>$@+
	@echo SANITIZE_LEAK=\''$(subst ','\'',$(subst ','\'',$(SANITIZE_LEAK)))'\' >>$@+
	@echo SANITIZE_ADDRESS=\''$(subst ','\'',$(subst ','\'',$(SANITIZE_ADDRESS)))'\' >>$@+
	@echo X=\'$(X)\' >>$@+
ifdef FSMONITOR_DAEMON_BACKEND
	@echo FSMONITOR_DAEMON_BACKEND=\''$(subst ','\'',$(subst ','\'',$(FSMONITOR_DAEMON_BACKEND)))'\' >>$@+
endif
ifdef FSMONITOR_OS_SETTINGS
	@echo FSMONITOR_OS_SETTINGS=\''$(subst ','\'',$(subst ','\'',$(FSMONITOR_OS_SETTINGS)))'\' >>$@+
endif
ifdef TEST_OUTPUT_DIRECTORY
	@echo TEST_OUTPUT_DIRECTORY=\''$(subst ','\'',$(subst ','\'',$(TEST_OUTPUT_DIRECTORY)))'\' >>$@+
endif
ifdef GIT_TEST_OPTS
	@echo GIT_TEST_OPTS=\''$(subst ','\'',$(subst ','\'',$(GIT_TEST_OPTS)))'\' >>$@+
endif
ifdef GIT_TEST_CMP
	@echo GIT_TEST_CMP=\''$(subst ','\'',$(subst ','\'',$(GIT_TEST_CMP)))'\' >>$@+
endif
ifdef GIT_TEST_CMP_USE_COPIED_CONTEXT
	@echo GIT_TEST_CMP_USE_COPIED_CONTEXT=YesPlease >>$@+
endif
ifdef GIT_TEST_UTF8_LOCALE
	@echo GIT_TEST_UTF8_LOCALE=\''$(subst ','\'',$(subst ','\'',$(GIT_TEST_UTF8_LOCALE)))'\' >>$@+
endif
	@echo NO_GETTEXT=\''$(subst ','\'',$(subst ','\'',$(NO_GETTEXT)))'\' >>$@+
ifdef GIT_PERF_REPEAT_COUNT
	@echo GIT_PERF_REPEAT_COUNT=\''$(subst ','\'',$(subst ','\'',$(GIT_PERF_REPEAT_COUNT)))'\' >>$@+
endif
ifdef GIT_PERF_REPO
	@echo GIT_PERF_REPO=\''$(subst ','\'',$(subst ','\'',$(GIT_PERF_REPO)))'\' >>$@+
endif
ifdef GIT_PERF_LARGE_REPO
	@echo GIT_PERF_LARGE_REPO=\''$(subst ','\'',$(subst ','\'',$(GIT_PERF_LARGE_REPO)))'\' >>$@+
endif
ifdef GIT_PERF_MAKE_OPTS
	@echo GIT_PERF_MAKE_OPTS=\''$(subst ','\'',$(subst ','\'',$(GIT_PERF_MAKE_OPTS)))'\' >>$@+
endif
ifdef GIT_PERF_MAKE_COMMAND
	@echo GIT_PERF_MAKE_COMMAND=\''$(subst ','\'',$(subst ','\'',$(GIT_PERF_MAKE_COMMAND)))'\' >>$@+
endif
ifdef GIT_INTEROP_MAKE_OPTS
	@echo GIT_INTEROP_MAKE_OPTS=\''$(subst ','\'',$(subst ','\'',$(GIT_INTEROP_MAKE_OPTS)))'\' >>$@+
endif
ifdef GIT_TEST_INDEX_VERSION
	@echo GIT_TEST_INDEX_VERSION=\''$(subst ','\'',$(subst ','\'',$(GIT_TEST_INDEX_VERSION)))'\' >>$@+
endif
ifdef GIT_TEST_PERL_FATAL_WARNINGS
	@echo GIT_TEST_PERL_FATAL_WARNINGS=\''$(subst ','\'',$(subst ','\'',$(GIT_TEST_PERL_FATAL_WARNINGS)))'\' >>$@+
endif
ifdef RUNTIME_PREFIX
	@echo RUNTIME_PREFIX=\'true\' >>$@+
else
	@echo RUNTIME_PREFIX=\'false\' >>$@+
endif
	@if cmp $@+ $@ >/dev/null 2>&1; then $(RM) $@+; else mv $@+ $@; fi
	@if test -f GIT-BUILD-DIR; then rm GIT-BUILD-DIR; fi

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

all:: $(TEST_PROGRAMS) $(test_bindir_programs) $(UNIT_TEST_PROGS) $(CLAR_TEST_PROG)

bin-wrappers/%: wrap-for-bin.sh
	$(call mkdir_p_parent_template)
	$(QUIET_GEN)sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
	     -e 's|@@BUILD_DIR@@|$(shell pwd)|' \
	     -e 's|@@PROG@@|$(patsubst test-%,t/helper/test-%,$(@F))$(if $(filter-out $(BINDIR_PROGRAMS_NO_X),$(@F)),$(X),)|' < $< > $@ && \
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

.PRECIOUS: $(TEST_OBJS)

t/helper/test-tool$X: $(patsubst %,t/helper/%,$(TEST_BUILTINS_OBJS)) $(UNIT_TEST_DIR)/test-lib.o

t/helper/test-%$X: t/helper/test-%.o GIT-LDFLAGS $(GITLIBS)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) $(filter %.a,$^) $(LIBS)

check-sha1:: t/helper/test-tool$X
	t/helper/test-sha1.sh

SP_SRC = $(filter-out $(THIRD_PARTY_SOURCES),$(patsubst %.o,%.c,$(OBJECTS)))
SP_OBJ = $(patsubst %.c,%.sp,$(SP_SRC))

$(SP_OBJ): %.sp: %.c %.o $(GENERATED_H)
	$(QUIET_SP)cgcc -no-compile $(ALL_CFLAGS) $(EXTRA_CPPFLAGS) \
		-Wsparse-error \
		$(SPARSE_FLAGS) $(SP_EXTRA_FLAGS) $< && \
	>$@

.PHONY: sparse
sparse: $(SP_OBJ)

EXCEPT_HDRS := $(GENERATED_H) unicode-width.h compat/% xdiff/% $(UNIT_TEST_DIR)/clar/% $(UNIT_TEST_DIR)/clar/clar/%
ifndef OPENSSL_SHA1
	EXCEPT_HDRS += sha1/openssl.h
endif
ifndef OPENSSL_SHA256
	EXCEPT_HDRS += sha256/openssl.h
endif
ifndef NETTLE_SHA256
	EXCEPT_HDRS += sha256/nettle.h
endif
ifndef GCRYPT_SHA256
	EXCEPT_HDRS += sha256/gcrypt.h
endif
CHK_HDRS = $(filter-out $(EXCEPT_HDRS),$(LIB_H))
HCO = $(patsubst %.h,%.hco,$(CHK_HDRS))
HCC = $(HCO:hco=hcc)

%.hcc: %.h
	@echo '#include "git-compat-util.h"' >$@
	@echo '#include "$<"' >>$@

$(HCO): %.hco: %.hcc $(GENERATED_H) FORCE
	$(QUIET_HDR)$(CC) $(ALL_CFLAGS) -o /dev/null -c -xc $<

.PHONY: hdr-check $(HCO)
hdr-check: $(HCO)

.PHONY: style
style:
	git clang-format --style file --diff --extensions c,h

.PHONY: check
check:
	@if sparse; \
	then \
		echo >&2 "Use 'make sparse' instead"; \
		$(MAKE) --no-print-directory sparse; \
	else \
		echo >&2 "Did you mean 'make test'?"; \
		exit 1; \
	fi

COCCI_GEN_ALL = .build/contrib/coccinelle/ALL.cocci
COCCI_GLOB = $(wildcard contrib/coccinelle/*.cocci)
COCCI_RULES_TRACKED = $(COCCI_GLOB:%=.build/%)
COCCI_RULES_TRACKED_NO_PENDING = $(filter-out %.pending.cocci,$(COCCI_RULES_TRACKED))
COCCI_RULES =
COCCI_RULES += $(COCCI_GEN_ALL)
COCCI_RULES += $(COCCI_RULES_TRACKED)
COCCI_NAMES =
COCCI_NAMES += $(COCCI_RULES:.build/contrib/coccinelle/%.cocci=%)

COCCICHECK_PENDING = $(filter %.pending.cocci,$(COCCI_RULES))
COCCICHECK = $(filter-out $(COCCICHECK_PENDING),$(COCCI_RULES))

COCCICHECK_PATCHES = $(COCCICHECK:%=%.patch)
COCCICHECK_PATCHES_PENDING = $(COCCICHECK_PENDING:%=%.patch)

COCCICHECK_PATCHES_INTREE = $(COCCICHECK_PATCHES:.build/%=%)
COCCICHECK_PATCHES_PENDING_INTREE = $(COCCICHECK_PATCHES_PENDING:.build/%=%)

# It's expensive to compute the many=many rules below, only eval them
# on $(MAKECMDGOALS) that match these $(COCCI_RULES)
COCCI_RULES_GLOB =
COCCI_RULES_GLOB += cocci%
COCCI_RULES_GLOB += .build/contrib/coccinelle/%
COCCI_RULES_GLOB += $(COCCICHECK_PATCHES)
COCCI_RULES_GLOB += $(COCCICHEC_PATCHES_PENDING)
COCCI_RULES_GLOB += $(COCCICHECK_PATCHES_INTREE)
COCCI_RULES_GLOB += $(COCCICHECK_PATCHES_PENDING_INTREE)
COCCI_GOALS = $(filter $(COCCI_RULES_GLOB),$(MAKECMDGOALS))

COCCI_TEST_RES = $(wildcard contrib/coccinelle/tests/*.res)

$(COCCI_RULES_TRACKED): .build/% : %
	$(call mkdir_p_parent_template)
	$(QUIET_CP)cp $< $@

.build/contrib/coccinelle/FOUND_H_SOURCES: $(FOUND_H_SOURCES)
	$(call mkdir_p_parent_template)
	$(QUIET_GEN) >$@

$(COCCI_GEN_ALL): $(COCCI_RULES_TRACKED_NO_PENDING)
	$(call mkdir_p_parent_template)
	$(QUIET_SPATCH_CAT)cat $^ >$@

ifeq ($(COMPUTE_HEADER_DEPENDENCIES),no)
SPATCH_USE_O_DEPENDENCIES =
endif
define cocci-rule

## Rule for .build/$(1).patch/$(2); Params:
# $(1) = e.g. ".build/contrib/coccinelle/free.cocci"
# $(2) = e.g. "grep.c"
# $(3) = e.g. "grep.o"
COCCI_$(1:.build/contrib/coccinelle/%.cocci=%) += $(1).d/$(2).patch
$(1).d/$(2).patch: GIT-SPATCH-DEFINES
$(1).d/$(2).patch: $(if $(and $(SPATCH_USE_O_DEPENDENCIES),$(wildcard $(3))),$(3),.build/contrib/coccinelle/FOUND_H_SOURCES)
$(1).d/$(2).patch: $(1)
$(1).d/$(2).patch: $(1).d/%.patch : %
	$$(call mkdir_p_parent_template)
	$$(QUIET_SPATCH)if ! $$(SPATCH) $$(SPATCH_FLAGS) \
		$$(SPATCH_INCLUDE_FLAGS) \
		--sp-file $(1) --patch . $$< \
		>$$@ 2>$$@.log; \
	then \
		echo "ERROR when applying '$(1)' to '$$<'; '$$@.log' follows:"; \
		cat $$@.log; \
		exit 1; \
	fi
endef

define cocci-matrix

$(foreach s,$(COCCI_SOURCES),$(call cocci-rule,$(c),$(s),$(s:%.c=%.o)))
endef

ifdef COCCI_GOALS
$(eval $(foreach c,$(COCCI_RULES),$(call cocci-matrix,$(c))))
endif

define spatch-rule

.build/contrib/coccinelle/$(1).cocci.patch: $$(COCCI_$(1))
	$$(QUIET_SPATCH_CAT)cat $$^ >$$@ && \
	if test -s $$@; \
	then \
		echo '    ' SPATCH result: $$@; \
	fi
contrib/coccinelle/$(1).cocci.patch: .build/contrib/coccinelle/$(1).cocci.patch
	$$(QUIET_CP)cp $$< $$@

endef

ifdef COCCI_GOALS
$(eval $(foreach n,$(COCCI_NAMES),$(call spatch-rule,$(n))))
endif

COCCI_TEST_RES_GEN = $(addprefix .build/,$(COCCI_TEST_RES))
$(COCCI_TEST_RES_GEN): GIT-SPATCH-DEFINES
$(COCCI_TEST_RES_GEN): .build/%.res : %.c
$(COCCI_TEST_RES_GEN): .build/%.res : %.res
ifdef SPATCH_CONCAT_COCCI
$(COCCI_TEST_RES_GEN): .build/contrib/coccinelle/tests/%.res : $(COCCI_GEN_ALL)
else
$(COCCI_TEST_RES_GEN): .build/contrib/coccinelle/tests/%.res : contrib/coccinelle/%.cocci
endif
	$(call mkdir_p_parent_template)
	$(QUIET_SPATCH_TEST)$(SPATCH) $(SPATCH_TEST_FLAGS) \
		--very-quiet --no-show-diff \
		--sp-file $< -o $@ \
		$(@:.build/%.res=%.c) && \
	cmp $(@:.build/%=%) $@ || \
	git -P diff --no-index $(@:.build/%=%) $@ 2>/dev/null; \

.PHONY: coccicheck-test
coccicheck-test: $(COCCI_TEST_RES_GEN)

coccicheck: coccicheck-test
ifdef SPATCH_CONCAT_COCCI
coccicheck: contrib/coccinelle/ALL.cocci.patch
else
coccicheck: $(COCCICHECK_PATCHES_INTREE)
endif

# See contrib/coccinelle/README
coccicheck-pending: coccicheck-test
coccicheck-pending: $(COCCICHECK_PATCHES_PENDING_INTREE)

.PHONY: coccicheck coccicheck-pending

# "Sub"-Makefiles, not really because they can't be run stand-alone,
# only there to contain directory-specific rules and variables
## gitweb/Makefile inclusion:
MAK_DIR_GITWEB = gitweb/
include gitweb/Makefile

.PHONY: gitweb
gitweb: $(MAK_DIR_GITWEB_ALL)
ifndef NO_GITWEB
all:: gitweb
endif

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

install_bindir_xprograms := $(patsubst %,%$X,$(BINDIR_PROGRAMS_NEED_X))
install_bindir_programs := $(install_bindir_xprograms) $(BINDIR_PROGRAMS_NO_X)

.PHONY: profile-install profile-fast-install
profile-install: profile
	$(MAKE) install

profile-fast-install: profile-fast
	$(MAKE) install

INSTALL_STRIP =

install: all
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(bindir_SQ)'
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(gitexec_instdir_SQ)'
	$(INSTALL) $(INSTALL_STRIP) $(PROGRAMS) '$(DESTDIR_SQ)$(gitexec_instdir_SQ)'
	$(INSTALL) $(SCRIPTS) '$(DESTDIR_SQ)$(gitexec_instdir_SQ)'
	$(INSTALL) -m 644 $(SCRIPT_LIB) '$(DESTDIR_SQ)$(gitexec_instdir_SQ)'
	$(INSTALL) $(INSTALL_STRIP) $(install_bindir_xprograms) '$(DESTDIR_SQ)$(bindir_SQ)'
	$(INSTALL) $(BINDIR_PROGRAMS_NO_X) '$(DESTDIR_SQ)$(bindir_SQ)'

ifdef MSVC
	# We DO NOT install the individual foo.o.pdb files because they
	# have already been rolled up into the exe's pdb file.
	# We DO NOT have pdb files for the builtin commands (like git-status.exe)
	# because it is just a copy/hardlink of git.exe, rather than a unique binary.
	$(INSTALL) $(patsubst %.exe,%.pdb,$(filter-out $(BUILT_INS),$(patsubst %,%$X,$(BINDIR_PROGRAMS_NEED_X)))) '$(DESTDIR_SQ)$(bindir_SQ)'
	$(INSTALL) $(patsubst %.exe,%.pdb,$(filter-out $(BUILT_INS) $(REMOTE_CURL_ALIASES),$(PROGRAMS))) '$(DESTDIR_SQ)$(gitexec_instdir_SQ)'
ifndef DEBUG
	$(INSTALL) $(vcpkg_rel_bin)/*.dll '$(DESTDIR_SQ)$(bindir_SQ)'
	$(INSTALL) $(vcpkg_rel_bin)/*.pdb '$(DESTDIR_SQ)$(bindir_SQ)'
else
	$(INSTALL) $(vcpkg_dbg_bin)/*.dll '$(DESTDIR_SQ)$(bindir_SQ)'
	$(INSTALL) $(vcpkg_dbg_bin)/*.pdb '$(DESTDIR_SQ)$(bindir_SQ)'
endif
endif
	$(MAKE) -C templates DESTDIR='$(DESTDIR_SQ)' install
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(mergetools_instdir_SQ)'
	$(INSTALL) -m 644 mergetools/* '$(DESTDIR_SQ)$(mergetools_instdir_SQ)'
ifndef NO_GETTEXT
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(localedir_SQ)'
	(cd po/build/locale && $(TAR) cf - .) | \
	(cd '$(DESTDIR_SQ)$(localedir_SQ)' && umask 022 && $(TAR) xof -)
endif
ifndef NO_PERL
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(perllibdir_SQ)'
	(cd perl/build/lib && $(TAR) cf - .) | \
	(cd '$(DESTDIR_SQ)$(perllibdir_SQ)' && umask 022 && $(TAR) xof -)
endif
ifndef NO_TCLTK
	$(MAKE) -C gitk-git install
	$(MAKE) -C git-gui gitexecdir='$(gitexec_instdir_SQ)' install
endif
ifneq (,$X)
	$(foreach p,$(patsubst %$X,%,$(filter %$X,$(ALL_COMMANDS_TO_INSTALL) $(OTHER_PROGRAMS))), test '$(DESTDIR_SQ)$(gitexec_instdir_SQ)/$p' -ef '$(DESTDIR_SQ)$(gitexec_instdir_SQ)/$p$X' || $(RM) '$(DESTDIR_SQ)$(gitexec_instdir_SQ)/$p';)
endif

	bindir=$$(cd '$(DESTDIR_SQ)$(bindir_SQ)' && pwd) && \
	execdir=$$(cd '$(DESTDIR_SQ)$(gitexec_instdir_SQ)' && pwd) && \
	destdir_from_execdir_SQ=$$(echo '$(gitexecdir_relative_SQ)' | sed -e 's|[^/][^/]*|..|g') && \
	{ test "$$bindir/" = "$$execdir/" || \
	  for p in $(OTHER_PROGRAMS) $(filter $(install_bindir_programs),$(ALL_PROGRAMS)); do \
		$(RM) "$$execdir/$$p" && \
		test -n "$(INSTALL_SYMLINKS)" && \
		ln -s "$$destdir_from_execdir_SQ/$(bindir_relative_SQ)/$$p" "$$execdir/$$p" || \
		{ test -z "$(NO_INSTALL_HARDLINKS)$(NO_CROSS_DIRECTORY_HARDLINKS)" && \
		  ln "$$bindir/$$p" "$$execdir/$$p" 2>/dev/null || \
		  cp "$$bindir/$$p" "$$execdir/$$p" || exit; } \
	  done; \
	} && \
	for p in $(filter $(install_bindir_programs),$(BUILT_INS)); do \
		$(RM) "$$bindir/$$p" && \
		test -n "$(INSTALL_SYMLINKS)" && \
		ln -s "git$X" "$$bindir/$$p" || \
		{ test -z "$(NO_INSTALL_HARDLINKS)" && \
		  ln "$$bindir/git$X" "$$bindir/$$p" 2>/dev/null || \
		  ln -s "git$X" "$$bindir/$$p" 2>/dev/null || \
		  cp "$$bindir/git$X" "$$bindir/$$p" || exit; }; \
	done && \
	for p in $(BUILT_INS); do \
		$(RM) "$$execdir/$$p" && \
		if test -z "$(SKIP_DASHED_BUILT_INS)"; \
		then \
			test -n "$(INSTALL_SYMLINKS)" && \
			ln -s "$$destdir_from_execdir_SQ/$(bindir_relative_SQ)/git$X" "$$execdir/$$p" || \
			{ test -z "$(NO_INSTALL_HARDLINKS)" && \
			  ln "$$execdir/git$X" "$$execdir/$$p" 2>/dev/null || \
			  ln -s "git$X" "$$execdir/$$p" 2>/dev/null || \
			  cp "$$execdir/git$X" "$$execdir/$$p" || exit; }; \
		fi \
	done && \
	remote_curl_aliases="$(REMOTE_CURL_ALIASES)" && \
	for p in $$remote_curl_aliases; do \
		$(RM) "$$execdir/$$p" && \
		test -n "$(INSTALL_SYMLINKS)" && \
		ln -s "git-remote-http$X" "$$execdir/$$p" || \
		{ test -z "$(NO_INSTALL_HARDLINKS)" && \
		  ln "$$execdir/git-remote-http$X" "$$execdir/$$p" 2>/dev/null || \
		  ln -s "git-remote-http$X" "$$execdir/$$p" 2>/dev/null || \
		  cp "$$execdir/git-remote-http$X" "$$execdir/$$p" || exit; } \
	done

.PHONY: install-doc install-man install-man-perl install-html install-info install-pdf
.PHONY: quick-install-doc quick-install-man quick-install-html

install-doc: install-man-perl
	$(MAKE) -C Documentation install

install-man: install-man-perl
	$(MAKE) -C Documentation install-man

install-man-perl: man-perl
	$(INSTALL) -d -m 755 '$(DESTDIR_SQ)$(mandir_SQ)/man3'
	(cd perl/build/man/man3 && $(TAR) cf - .) | \
	(cd '$(DESTDIR_SQ)$(mandir_SQ)/man3' && umask 022 && $(TAR) xof -)

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

GIT_TARNAME = git-$(GIT_VERSION)
GIT_ARCHIVE_EXTRA_FILES = \
	--prefix=$(GIT_TARNAME)/ \
	--add-file=configure \
	--add-file=.dist-tmp-dir/version \
	--prefix=$(GIT_TARNAME)/git-gui/ \
	--add-file=.dist-tmp-dir/git-gui/version
ifdef DC_SHA1_SUBMODULE
GIT_ARCHIVE_EXTRA_FILES += \
	--prefix=$(GIT_TARNAME)/sha1collisiondetection/ \
	--add-file=sha1collisiondetection/LICENSE.txt \
	--prefix=$(GIT_TARNAME)/sha1collisiondetection/lib/ \
	--add-file=sha1collisiondetection/lib/sha1.c \
	--add-file=sha1collisiondetection/lib/sha1.h \
	--add-file=sha1collisiondetection/lib/ubc_check.c \
	--add-file=sha1collisiondetection/lib/ubc_check.h
endif
dist: git-archive$(X) configure
	@$(RM) -r .dist-tmp-dir
	@mkdir .dist-tmp-dir
	@echo $(GIT_VERSION) > .dist-tmp-dir/version
	@$(MAKE) -C git-gui TARDIR=../.dist-tmp-dir/git-gui dist-version
	./git-archive --format=tar \
		$(GIT_ARCHIVE_EXTRA_FILES) \
		--prefix=$(GIT_TARNAME)/ HEAD^{tree} > $(GIT_TARNAME).tar
	@$(RM) -r .dist-tmp-dir
	gzip -f -9 $(GIT_TARNAME).tar

rpm::
	@echo >&2 "Use distro packaged sources to run rpmbuild"
	@false
.PHONY: rpm

ifneq ($(INCLUDE_DLLS_IN_ARTIFACTS),)
OTHER_PROGRAMS += $(shell echo *.dll t/helper/*.dll t/unit-tests/bin/*.dll)
endif

artifacts-tar:: $(ALL_COMMANDS_TO_INSTALL) $(SCRIPT_LIB) $(OTHER_PROGRAMS) \
		GIT-BUILD-OPTIONS $(TEST_PROGRAMS) $(test_bindir_programs) \
		$(UNIT_TEST_PROGS) $(CLAR_TEST_PROG) $(MOFILES)
	$(QUIET_SUBDIR0)templates $(QUIET_SUBDIR1) \
		SHELL_PATH='$(SHELL_PATH_SQ)' PERL_PATH='$(PERL_PATH_SQ)'
	test -n "$(ARTIFACTS_DIRECTORY)"
	mkdir -p "$(ARTIFACTS_DIRECTORY)"
	$(TAR) czf "$(ARTIFACTS_DIRECTORY)/artifacts.tar.gz" $^ templates/blt/
.PHONY: artifacts-tar

htmldocs = git-htmldocs-$(GIT_VERSION)
manpages = git-manpages-$(GIT_VERSION)
.PHONY: dist-doc distclean
dist-doc: git$X
	$(RM) -r .doc-tmp-dir
	mkdir .doc-tmp-dir
	$(MAKE) -C Documentation WEBDOC_DEST=../.doc-tmp-dir install-webdoc
	./git -C .doc-tmp-dir init
	./git -C .doc-tmp-dir add .
	./git -C .doc-tmp-dir commit -m htmldocs
	./git -C .doc-tmp-dir archive --format=tar --prefix=./ HEAD^{tree} \
		> $(htmldocs).tar
	gzip -n -9 -f $(htmldocs).tar
	:
	$(RM) -r .doc-tmp-dir
	mkdir -p .doc-tmp-dir/man1 .doc-tmp-dir/man5 .doc-tmp-dir/man7
	$(MAKE) -C Documentation DESTDIR=./ \
		man1dir=../.doc-tmp-dir/man1 \
		man5dir=../.doc-tmp-dir/man5 \
		man7dir=../.doc-tmp-dir/man7 \
		install
	./git -C .doc-tmp-dir init
	./git -C .doc-tmp-dir add .
	./git -C .doc-tmp-dir commit -m manpages
	./git -C .doc-tmp-dir archive --format=tar --prefix=./ HEAD^{tree} \
		> $(manpages).tar
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

cocciclean:
	$(RM) GIT-SPATCH-DEFINES
	$(RM) -r .build/contrib/coccinelle
	$(RM) contrib/coccinelle/*.cocci.patch

clean: profile-clean coverage-clean cocciclean
	$(RM) -r .build $(UNIT_TEST_BIN)
	$(RM) GIT-TEST-SUITES
	$(RM) po/git.pot po/git-core.pot
	$(RM) git.res
	$(RM) $(OBJECTS)
	$(RM) headless-git.o
	$(RM) $(LIB_FILE) $(XDIFF_LIB) $(REFTABLE_LIB)
	$(RM) $(ALL_PROGRAMS) $(SCRIPT_LIB) $(BUILT_INS) $(OTHER_PROGRAMS)
	$(RM) $(TEST_PROGRAMS)
	$(RM) $(FUZZ_PROGRAMS)
	$(RM) $(SP_OBJ)
	$(RM) $(HCC)
	$(RM) -r bin-wrappers $(dep_dirs) $(compdb_dir) compile_commands.json
	$(RM) -r po/build/
	$(RM) *.pyc *.pyo */*.pyc */*.pyo $(GENERATED_H) $(ETAGS_TARGET) tags cscope*
	$(RM) -r .dist-tmp-dir .doc-tmp-dir
	$(RM) $(GIT_TARNAME).tar.gz
	$(RM) $(htmldocs).tar.gz $(manpages).tar.gz
	$(MAKE) -C Documentation/ clean
	$(RM) Documentation/GIT-EXCLUDED-PROGRAMS
ifndef NO_PERL
	$(RM) -r perl/build/
endif
	$(MAKE) -C templates/ clean
	$(MAKE) -C t/ clean
ifndef NO_TCLTK
	$(MAKE) -C gitk-git clean
	$(MAKE) -C git-gui clean
endif
	$(RM) GIT-VERSION-FILE GIT-CFLAGS GIT-LDFLAGS GIT-BUILD-OPTIONS
	$(RM) GIT-USER-AGENT GIT-PREFIX
	$(RM) GIT-SCRIPT-DEFINES GIT-PERL-DEFINES GIT-PERL-HEADER GIT-PYTHON-VARS
ifdef MSVC
	$(RM) $(patsubst %.o,%.o.pdb,$(OBJECTS))
	$(RM) headless-git.o.pdb
	$(RM) $(patsubst %.exe,%.pdb,$(OTHER_PROGRAMS))
	$(RM) $(patsubst %.exe,%.iobj,$(OTHER_PROGRAMS))
	$(RM) $(patsubst %.exe,%.ipdb,$(OTHER_PROGRAMS))
	$(RM) $(patsubst %.exe,%.pdb,$(PROGRAMS))
	$(RM) $(patsubst %.exe,%.iobj,$(PROGRAMS))
	$(RM) $(patsubst %.exe,%.ipdb,$(PROGRAMS))
	$(RM) $(patsubst %.exe,%.pdb,$(TEST_PROGRAMS))
	$(RM) $(patsubst %.exe,%.iobj,$(TEST_PROGRAMS))
	$(RM) $(patsubst %.exe,%.ipdb,$(TEST_PROGRAMS))
	$(RM) compat/vcbuild/MSVC-DEFS-GEN
endif

.PHONY: all install profile-clean cocciclean clean strip
.PHONY: shell_compatibility_test please_set_SHELL_PATH_to_a_more_modern_shell
.PHONY: FORCE

### Check documentation
#
ALL_COMMANDS = $(ALL_COMMANDS_TO_INSTALL) $(SCRIPT_LIB)
ALL_COMMANDS += git
ALL_COMMANDS += git-citool
ALL_COMMANDS += git-gui
ALL_COMMANDS += gitk
ALL_COMMANDS += gitweb
ALL_COMMANDS += scalar

.PHONY: check-docs
check-docs::
	$(MAKE) -C Documentation lint-docs

### Make sure built-ins do not have dups and listed in git.c
#
check-builtins::
	./check-builtins.sh

### Test suite coverage testing
#
.PHONY: coverage coverage-clean coverage-compile coverage-test coverage-report
.PHONY: coverage-untested-functions cover_db cover_db_html
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
	$(RM) coverage-test.made

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
	touch coverage-test.made

coverage-test.made:
	$(MAKE) coverage-test

coverage-prove: coverage-clean-results coverage-compile
	$(MAKE) CFLAGS="$(COVERAGE_CFLAGS)" LDFLAGS="$(COVERAGE_LDFLAGS)" \
		DEFAULT_TEST_TARGET=prove GIT_PROVE_OPTS="$(GIT_PROVE_OPTS) -j1" \
		-j1 test

coverage-report: coverage-test.made
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


### Fuzz testing
#
# Building fuzz targets generally requires a special set of compiler flags that
# are not necessarily appropriate for general builds, and that vary greatly
# depending on the compiler version used.
#
# An example command to build against libFuzzer from LLVM 11.0.0:
#
# make CC=clang FUZZ_CXX=clang++ \
#      CFLAGS="-fsanitize=fuzzer-no-link,address" \
#      LIB_FUZZING_ENGINE="-fsanitize=fuzzer,address" \
#      fuzz-all
#
FUZZ_CXX ?= $(CC)
FUZZ_CXXFLAGS ?= $(ALL_CFLAGS)

.PHONY: fuzz-all
fuzz-all: $(FUZZ_PROGRAMS)

$(FUZZ_PROGRAMS): %: %.o oss-fuzz/dummy-cmd-main.o $(GITLIBS) GIT-LDFLAGS
	$(QUIET_LINK)$(FUZZ_CXX) $(FUZZ_CXXFLAGS) -o $@ $(ALL_LDFLAGS) \
		-Wl,--allow-multiple-definition \
		$(filter %.o,$^) $(filter %.a,$^) $(LIBS) $(LIB_FUZZING_ENGINE)

$(UNIT_TEST_PROGS): $(UNIT_TEST_BIN)/%$X: $(UNIT_TEST_DIR)/%.o $(UNIT_TEST_OBJS) \
	$(GITLIBS) GIT-LDFLAGS
	$(call mkdir_p_parent_template)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) \
		$(filter %.o,$^) $(filter %.a,$^) $(LIBS)

GIT-TEST-SUITES: FORCE
	@FLAGS='$(CLAR_TEST_SUITES)'; \
	    if test x"$$FLAGS" != x"`cat GIT-TEST-SUITES 2>/dev/null`" ; then \
		echo >&2 "    * new test suites"; \
		echo "$$FLAGS" >GIT-TEST-SUITES; \
            fi

$(UNIT_TEST_DIR)/clar-decls.h: $(patsubst %,$(UNIT_TEST_DIR)/%.c,$(CLAR_TEST_SUITES)) GIT-TEST-SUITES
	$(QUIET_GEN)$(SHELL_PATH) $(UNIT_TEST_DIR)/generate-clar-decls.sh "$@" $(filter %.c,$^)
$(UNIT_TEST_DIR)/clar.suite: $(UNIT_TEST_DIR)/clar-decls.h
	$(QUIET_GEN)awk -f $(UNIT_TEST_DIR)/clar-generate.awk $< >$(UNIT_TEST_DIR)/clar.suite
$(UNIT_TEST_DIR)/clar/clar.o: $(UNIT_TEST_DIR)/clar.suite
$(CLAR_TEST_OBJS): $(UNIT_TEST_DIR)/clar-decls.h
$(CLAR_TEST_OBJS): EXTRA_CPPFLAGS = -I$(UNIT_TEST_DIR)
$(CLAR_TEST_PROG): $(UNIT_TEST_DIR)/clar.suite $(CLAR_TEST_OBJS) $(GITLIBS) GIT-LDFLAGS
	$(call mkdir_p_parent_template)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) $(LIBS)

.PHONY: build-unit-tests unit-tests
build-unit-tests: $(UNIT_TEST_PROGS) $(CLAR_TEST_PROG)
unit-tests: $(UNIT_TEST_PROGS) $(CLAR_TEST_PROG) t/helper/test-tool$X
	$(MAKE) -C t/ unit-tests
