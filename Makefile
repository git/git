# The default target of this Makefile is...
all:

# Define MOZILLA_SHA1 environment variable when running make to make use of
# a bundled SHA1 routine coming from Mozilla. It is GPL'd and should be fast
# on non-x86 architectures (e.g. PowerPC), while the OpenSSL version (default
# choice) has very fast version optimized for i586.
#
# Define NO_OPENSSL environment variable if you do not have OpenSSL.
# This also implies MOZILLA_SHA1.
#
# Define NO_CURL if you do not have curl installed.  git-http-pull and
# git-http-push are not built, and you cannot use http:// and https://
# transports.
#
# Define CURLDIR=/foo/bar if your curl header and library files are in
# /foo/bar/include and /foo/bar/lib directories.
#
# Define NO_EXPAT if you do not have expat installed.  git-http-push is
# not built, and you cannot push using http:// and https:// transports.
#
# Define NO_D_INO_IN_DIRENT if you don't have d_ino in your struct dirent.
#
# Define NO_D_TYPE_IN_DIRENT if your platform defines DT_UNKNOWN but lacks
# d_type in struct dirent (latest Cygwin -- will be fixed soonish).
#
# Define NO_STRCASESTR if you don't have strcasestr.
#
# Define NO_SETENV if you don't have setenv in the C library.
#
# Define NO_SYMLINK_HEAD if you never want .git/HEAD to be a symbolic link.
# Enable it on Windows.  By default, symrefs are still used.
#
# Define PPC_SHA1 environment variable when running make to make use of
# a bundled SHA1 routine optimized for PowerPC.
#
# Define ARM_SHA1 environment variable when running make to make use of
# a bundled SHA1 routine optimized for ARM.
#
# Define NEEDS_SSL_WITH_CRYPTO if you need -lcrypto with -lssl (Darwin).
#
# Define NEEDS_LIBICONV if linking with libc is not enough (Darwin).
#
# Define NEEDS_SOCKET if linking with libc is not enough (SunOS,
# Patrick Mauritz).
#
# Define NO_MMAP if you want to avoid mmap.
#
# Define WITH_OWN_SUBPROCESS_PY if you want to use with python 2.3.
#
# Define NO_IPV6 if you lack IPv6 support and getaddrinfo().
#
# Define NO_SOCKADDR_STORAGE if your platform does not have struct
# sockaddr_storage.
#
# Define NO_ICONV if your libc does not properly support iconv.
#
# Define NO_ACCURATE_DIFF if your diff program at least sometimes misses
# a missing newline at the end of the file.
#
# Define NO_PYTHON if you want to loose all benefits of the recursive merge.
#
# Define COLLISION_CHECK below if you believe that SHA1's
# 1461501637330902918203684832716283019655932542976 hashes do not give you
# sufficient guarantee that no collisions between objects will ever happen.

# Define USE_NSEC below if you want git to care about sub-second file mtimes
# and ctimes. Note that you need recent glibc (at least 2.2.4) for this, and
# it will BREAK YOUR LOCAL DIFFS! show-diff and anything using it will likely
# randomly break unless your underlying filesystem supports those sub-second
# times (my ext3 doesn't).

# Define USE_STDEV below if you want git to care about the underlying device
# change being considered an inode change from the update-cache perspective.

GIT-VERSION-FILE: .FORCE-GIT-VERSION-FILE
	@$(SHELL_PATH) ./GIT-VERSION-GEN
-include GIT-VERSION-FILE

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
uname_M := $(shell sh -c 'uname -m 2>/dev/null || echo not')
uname_O := $(shell sh -c 'uname -o 2>/dev/null || echo not')
uname_R := $(shell sh -c 'uname -r 2>/dev/null || echo not')
uname_P := $(shell sh -c 'uname -p 2>/dev/null || echo not')

# CFLAGS and LDFLAGS are for the users to override from the command line.

CFLAGS = -g -O2 -Wall
LDFLAGS =
ALL_CFLAGS = $(CFLAGS)
ALL_LDFLAGS = $(LDFLAGS)
STRIP ?= strip

prefix = $(HOME)
bindir = $(prefix)/bin
gitexecdir = $(bindir)
template_dir = $(prefix)/share/git-core/templates/
GIT_PYTHON_DIR = $(prefix)/share/git-core/python
# DESTDIR=

CC = gcc
AR = ar
TAR = tar
INSTALL = install
RPMBUILD = rpmbuild

# sparse is architecture-neutral, which means that we need to tell it
# explicitly what architecture to check for. Fix this up for yours..
SPARSE_FLAGS = -D__BIG_ENDIAN__ -D__powerpc__



### --- END CONFIGURATION SECTION ---

SCRIPT_SH = \
	git-bisect.sh git-branch.sh git-checkout.sh \
	git-cherry.sh git-clean.sh git-clone.sh git-commit.sh \
	git-fetch.sh \
	git-format-patch.sh git-ls-remote.sh \
	git-merge-one-file.sh git-parse-remote.sh \
	git-prune.sh git-pull.sh git-rebase.sh \
	git-repack.sh git-request-pull.sh git-reset.sh \
	git-resolve.sh git-revert.sh git-sh-setup.sh \
	git-tag.sh git-verify-tag.sh \
	git-applymbox.sh git-applypatch.sh git-am.sh \
	git-merge.sh git-merge-stupid.sh git-merge-octopus.sh \
	git-merge-resolve.sh git-merge-ours.sh \
	git-lost-found.sh

SCRIPT_PERL = \
	git-archimport.perl git-cvsimport.perl git-relink.perl \
	git-shortlog.perl git-fmt-merge-msg.perl git-rerere.perl \
	git-annotate.perl git-cvsserver.perl \
	git-svnimport.perl git-mv.perl git-cvsexportcommit.perl \
	git-send-email.perl

SCRIPT_PYTHON = \
	git-merge-recursive.py

SCRIPTS = $(patsubst %.sh,%,$(SCRIPT_SH)) \
	  $(patsubst %.perl,%,$(SCRIPT_PERL)) \
	  $(patsubst %.py,%,$(SCRIPT_PYTHON)) \
	  git-cherry-pick git-status

# The ones that do not have to link with lcrypto, lz nor xdiff.
SIMPLE_PROGRAMS = \
	git-get-tar-commit-id$X git-mailsplit$X \
	git-stripspace$X git-daemon$X

# ... and all the rest that could be moved out of bindir to gitexecdir
PROGRAMS = \
	git-apply$X git-cat-file$X \
	git-checkout-index$X git-clone-pack$X git-commit-tree$X \
	git-convert-objects$X git-diff-files$X \
	git-diff-index$X git-diff-stages$X \
	git-diff-tree$X git-fetch-pack$X git-fsck-objects$X \
	git-hash-object$X git-index-pack$X git-init-db$X git-local-fetch$X \
	git-ls-files$X git-ls-tree$X git-mailinfo$X git-merge-base$X \
	git-merge-index$X git-mktag$X git-mktree$X git-pack-objects$X git-patch-id$X \
	git-peek-remote$X git-prune-packed$X git-read-tree$X \
	git-receive-pack$X git-rev-list$X git-rev-parse$X \
	git-send-pack$X git-show-branch$X git-shell$X \
	git-show-index$X git-ssh-fetch$X \
	git-ssh-upload$X git-tar-tree$X git-unpack-file$X \
	git-unpack-objects$X git-update-index$X git-update-server-info$X \
	git-upload-pack$X git-verify-pack$X git-write-tree$X \
	git-update-ref$X git-symbolic-ref$X git-check-ref-format$X \
	git-name-rev$X git-pack-redundant$X git-repo-config$X git-var$X \
	git-describe$X git-merge-tree$X git-blame$X git-imap-send$X

BUILT_INS = git-log$X git-whatchanged$X git-show$X \
	git-count-objects$X git-diff$X git-push$X \
	git-grep$X git-add$X git-rm$X git-rev-list$X \
	git-check-ref-format$X

# what 'all' will build and 'install' will install, in gitexecdir
ALL_PROGRAMS = $(PROGRAMS) $(SIMPLE_PROGRAMS) $(SCRIPTS)

# Backward compatibility -- to be removed after 1.0
PROGRAMS += git-ssh-pull$X git-ssh-push$X

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

PYMODULES = \
	gitMergeCommon.py

LIB_FILE=libgit.a
XDIFF_LIB=xdiff/lib.a

LIB_H = \
	blob.h cache.h commit.h csum-file.h delta.h \
	diff.h object.h pack.h pkt-line.h quote.h refs.h \
	run-command.h strbuf.h tag.h tree.h git-compat-util.h revision.h \
	tree-walk.h log-tree.h dir.h

DIFF_OBJS = \
	diff.o diff-lib.o diffcore-break.o diffcore-order.o \
	diffcore-pickaxe.o diffcore-rename.o tree-diff.o combine-diff.o \
	diffcore-delta.o log-tree.o

LIB_OBJS = \
	blob.o commit.o connect.o csum-file.o base85.o \
	date.o diff-delta.o entry.o exec_cmd.o ident.o index.o \
	object.o pack-check.o patch-delta.o path.o pkt-line.o \
	quote.o read-cache.o refs.o run-command.o dir.o \
	server-info.o setup.o sha1_file.o sha1_name.o strbuf.o \
	tag.o tree.o usage.o config.o environment.o ctype.o copy.o \
	fetch-clone.o revision.o pager.o tree-walk.o xdiff-interface.o \
	$(DIFF_OBJS)

BUILTIN_OBJS = \
	builtin-log.o builtin-help.o builtin-count.o builtin-diff.o builtin-push.o \
	builtin-grep.o builtin-add.o builtin-rev-list.o builtin-check-ref-format.o \
	builtin-rm.o

GITLIBS = $(LIB_FILE) $(XDIFF_LIB)
LIBS = $(GITLIBS) -lz

#
# Platform specific tweaks
#

# We choose to avoid "if .. else if .. else .. endif endif"
# because maintaining the nesting to match is a pain.  If
# we had "elif" things would have been much nicer...

ifeq ($(uname_S),Darwin)
	NEEDS_SSL_WITH_CRYPTO = YesPlease
	NEEDS_LIBICONV = YesPlease
	## fink
	ifeq ($(shell test -d /sw/lib && echo y),y)
		ALL_CFLAGS += -I/sw/include
		ALL_LDFLAGS += -L/sw/lib
	endif
	## darwinports
	ifeq ($(shell test -d /opt/local/lib && echo y),y)
		ALL_CFLAGS += -I/opt/local/include
		ALL_LDFLAGS += -L/opt/local/lib
	endif
endif
ifeq ($(uname_S),SunOS)
	NEEDS_SOCKET = YesPlease
	NEEDS_NSL = YesPlease
	SHELL_PATH = /bin/bash
	NO_STRCASESTR = YesPlease
	ifeq ($(uname_R),5.8)
		NEEDS_LIBICONV = YesPlease
		NO_UNSETENV = YesPlease
		NO_SETENV = YesPlease
	endif
	ifeq ($(uname_R),5.9)
		NO_UNSETENV = YesPlease
		NO_SETENV = YesPlease
	endif
	INSTALL = ginstall
	TAR = gtar
	ALL_CFLAGS += -D__EXTENSIONS__
endif
ifeq ($(uname_O),Cygwin)
	NO_D_TYPE_IN_DIRENT = YesPlease
	NO_D_INO_IN_DIRENT = YesPlease
	NO_STRCASESTR = YesPlease
	NO_SYMLINK_HEAD = YesPlease
	NEEDS_LIBICONV = YesPlease
	# There are conflicting reports about this.
	# On some boxes NO_MMAP is needed, and not so elsewhere.
	# Try uncommenting this if you see things break -- YMMV.
	# NO_MMAP = YesPlease
	NO_IPV6 = YesPlease
	X = .exe
endif
ifeq ($(uname_S),FreeBSD)
	NEEDS_LIBICONV = YesPlease
	ALL_CFLAGS += -I/usr/local/include
	ALL_LDFLAGS += -L/usr/local/lib
endif
ifeq ($(uname_S),OpenBSD)
	NO_STRCASESTR = YesPlease
	NEEDS_LIBICONV = YesPlease
	ALL_CFLAGS += -I/usr/local/include
	ALL_LDFLAGS += -L/usr/local/lib
endif
ifeq ($(uname_S),NetBSD)
	ifeq ($(shell expr "$(uname_R)" : '[01]\.'),2)
		NEEDS_LIBICONV = YesPlease
	endif
	ALL_CFLAGS += -I/usr/pkg/include
	ALL_LDFLAGS += -L/usr/pkg/lib -Wl,-rpath,/usr/pkg/lib
endif
ifeq ($(uname_S),AIX)
	NO_STRCASESTR=YesPlease
	NEEDS_LIBICONV=YesPlease
endif
ifeq ($(uname_S),IRIX64)
	NO_IPV6=YesPlease
	NO_SETENV=YesPlease
	NO_STRCASESTR=YesPlease
	NO_SOCKADDR_STORAGE=YesPlease
	SHELL_PATH=/usr/gnu/bin/bash
	ALL_CFLAGS += -DPATH_MAX=1024
	# for now, build 32-bit version
	ALL_LDFLAGS += -L/usr/lib32
endif
ifneq (,$(findstring arm,$(uname_M)))
	ARM_SHA1 = YesPlease
endif

-include config.mak

ifdef WITH_OWN_SUBPROCESS_PY
	PYMODULES += compat/subprocess.py
else
	ifeq ($(NO_PYTHON),)
		ifneq ($(shell $(PYTHON_PATH) -c 'import subprocess;print"OK"' 2>/dev/null),OK)
			PYMODULES += compat/subprocess.py
		endif
	endif
endif

ifndef NO_CURL
	ifdef CURLDIR
		# This is still problematic -- gcc does not always want -R.
		ALL_CFLAGS += -I$(CURLDIR)/include
		CURL_LIBCURL = -L$(CURLDIR)/lib -R$(CURLDIR)/lib -lcurl
	else
		CURL_LIBCURL = -lcurl
	endif
	PROGRAMS += git-http-fetch$X
	curl_check := $(shell (echo 070908; curl-config --vernum) | sort -r | sed -ne 2p)
	ifeq "$(curl_check)" "070908"
		ifndef NO_EXPAT
			PROGRAMS += git-http-push$X
		endif
	endif
	ifndef NO_EXPAT
		EXPAT_LIBEXPAT = -lexpat
	endif
endif

ifndef NO_OPENSSL
	OPENSSL_LIBSSL = -lssl
	ifdef OPENSSLDIR
		# Again this may be problematic -- gcc does not always want -R.
		ALL_CFLAGS += -I$(OPENSSLDIR)/include
		OPENSSL_LINK = -L$(OPENSSLDIR)/lib -R$(OPENSSLDIR)/lib
	else
		OPENSSL_LINK =
	endif
else
	ALL_CFLAGS += -DNO_OPENSSL
	MOZILLA_SHA1 = 1
	OPENSSL_LIBSSL =
endif
ifdef NEEDS_SSL_WITH_CRYPTO
	LIB_4_CRYPTO = $(OPENSSL_LINK) -lcrypto -lssl
else
	LIB_4_CRYPTO = $(OPENSSL_LINK) -lcrypto
endif
ifdef NEEDS_LIBICONV
	ifdef ICONVDIR
		# Again this may be problematic -- gcc does not always want -R.
		ALL_CFLAGS += -I$(ICONVDIR)/include
		ICONV_LINK = -L$(ICONVDIR)/lib -R$(ICONVDIR)/lib
	else
		ICONV_LINK =
	endif
	LIB_4_ICONV = $(ICONV_LINK) -liconv
else
	LIB_4_ICONV =
endif
ifdef NEEDS_SOCKET
	LIBS += -lsocket
	SIMPLE_LIB += -lsocket
endif
ifdef NEEDS_NSL
	LIBS += -lnsl
	SIMPLE_LIB += -lnsl
endif
ifdef NO_D_TYPE_IN_DIRENT
	ALL_CFLAGS += -DNO_D_TYPE_IN_DIRENT
endif
ifdef NO_D_INO_IN_DIRENT
	ALL_CFLAGS += -DNO_D_INO_IN_DIRENT
endif
ifdef NO_SYMLINK_HEAD
	ALL_CFLAGS += -DNO_SYMLINK_HEAD
endif
ifdef NO_STRCASESTR
	COMPAT_CFLAGS += -DNO_STRCASESTR
	COMPAT_OBJS += compat/strcasestr.o
endif
ifdef NO_SETENV
	COMPAT_CFLAGS += -DNO_SETENV
	COMPAT_OBJS += compat/setenv.o
endif
ifdef NO_SETENV
	COMPAT_CFLAGS += -DNO_UNSETENV
	COMPAT_OBJS += compat/unsetenv.o
endif
ifdef NO_MMAP
	COMPAT_CFLAGS += -DNO_MMAP
	COMPAT_OBJS += compat/mmap.o
endif
ifdef NO_IPV6
	ALL_CFLAGS += -DNO_IPV6
endif
ifdef NO_SOCKADDR_STORAGE
ifdef NO_IPV6
	ALL_CFLAGS += -Dsockaddr_storage=sockaddr_in
else
	ALL_CFLAGS += -Dsockaddr_storage=sockaddr_in6
endif
endif

ifdef NO_ICONV
	ALL_CFLAGS += -DNO_ICONV
endif

ifdef PPC_SHA1
	SHA1_HEADER = "ppc/sha1.h"
	LIB_OBJS += ppc/sha1.o ppc/sha1ppc.o
else
ifdef ARM_SHA1
	SHA1_HEADER = "arm/sha1.h"
	LIB_OBJS += arm/sha1.o arm/sha1_arm.o
else
ifdef MOZILLA_SHA1
	SHA1_HEADER = "mozilla-sha1/sha1.h"
	LIB_OBJS += mozilla-sha1/sha1.o
else
	SHA1_HEADER = <openssl/sha.h>
	LIBS += $(LIB_4_CRYPTO)
endif
endif
endif
ifdef NO_ACCURATE_DIFF
	ALL_CFLAGS += -DNO_ACCURATE_DIFF
endif

# Shell quote (do not use $(call) to accomodate ancient setups);

SHA1_HEADER_SQ = $(subst ','\'',$(SHA1_HEADER))

DESTDIR_SQ = $(subst ','\'',$(DESTDIR))
bindir_SQ = $(subst ','\'',$(bindir))
gitexecdir_SQ = $(subst ','\'',$(gitexecdir))
template_dir_SQ = $(subst ','\'',$(template_dir))

SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))
PERL_PATH_SQ = $(subst ','\'',$(PERL_PATH))
PYTHON_PATH_SQ = $(subst ','\'',$(PYTHON_PATH))
GIT_PYTHON_DIR_SQ = $(subst ','\'',$(GIT_PYTHON_DIR))

ALL_CFLAGS += -DSHA1_HEADER='$(SHA1_HEADER_SQ)' $(COMPAT_CFLAGS)
LIB_OBJS += $(COMPAT_OBJS)
export prefix TAR INSTALL DESTDIR SHELL_PATH template_dir
### Build rules

all: $(ALL_PROGRAMS) $(BUILT_INS) git$X gitk

all:
	$(MAKE) -C templates

strip: $(PROGRAMS) git$X
	$(STRIP) $(STRIP_OPTS) $(PROGRAMS) git$X

git$X: git.c common-cmds.h $(BUILTIN_OBJS) $(GITLIBS)
	$(CC) -DGIT_VERSION='"$(GIT_VERSION)"' \
		$(ALL_CFLAGS) -o $@ $(filter %.c,$^) \
		$(BUILTIN_OBJS) $(ALL_LDFLAGS) $(LIBS)

builtin-help.o: common-cmds.h

$(BUILT_INS): git$X
	rm -f $@ && ln git$X $@

common-cmds.h: Documentation/git-*.txt
	./generate-cmdlist.sh > $@

$(patsubst %.sh,%,$(SCRIPT_SH)) : % : %.sh
	rm -f $@
	sed -e '1s|#!.*/sh|#!$(SHELL_PATH_SQ)|' \
	    -e 's/@@GIT_VERSION@@/$(GIT_VERSION)/g' \
	    -e 's/@@NO_CURL@@/$(NO_CURL)/g' \
	    -e 's/@@NO_PYTHON@@/$(NO_PYTHON)/g' \
	    $@.sh >$@
	chmod +x $@

$(patsubst %.perl,%,$(SCRIPT_PERL)) : % : %.perl
	rm -f $@
	sed -e '1s|#!.*perl|#!$(PERL_PATH_SQ)|' \
	    -e 's/@@GIT_VERSION@@/$(GIT_VERSION)/g' \
	    $@.perl >$@
	chmod +x $@

$(patsubst %.py,%,$(SCRIPT_PYTHON)) : % : %.py
	rm -f $@
	sed -e '1s|#!.*python|#!$(PYTHON_PATH_SQ)|' \
	    -e 's|@@GIT_PYTHON_PATH@@|$(GIT_PYTHON_DIR_SQ)|g' \
	    -e 's/@@GIT_VERSION@@/$(GIT_VERSION)/g' \
	    $@.py >$@
	chmod +x $@

git-cherry-pick: git-revert
	cp $< $@

git-status: git-commit
	cp $< $@

# These can record GIT_VERSION
git$X git.spec \
	$(patsubst %.sh,%,$(SCRIPT_SH)) \
	$(patsubst %.perl,%,$(SCRIPT_PERL)) \
	$(patsubst %.py,%,$(SCRIPT_PYTHON)) \
	: GIT-VERSION-FILE

%.o: %.c
	$(CC) -o $*.o -c $(ALL_CFLAGS) $<
%.o: %.S
	$(CC) -o $*.o -c $(ALL_CFLAGS) $<

exec_cmd.o: exec_cmd.c
	$(CC) -o $*.o -c $(ALL_CFLAGS) '-DGIT_EXEC_PATH="$(gitexecdir_SQ)"' $<

http.o: http.c
	$(CC) -o $*.o -c $(ALL_CFLAGS) -DGIT_USER_AGENT='"git/$(GIT_VERSION)"' $<

ifdef NO_EXPAT
http-fetch.o: http-fetch.c
	$(CC) -o $*.o -c $(ALL_CFLAGS) -DNO_EXPAT $<
endif

git-%$X: %.o $(GITLIBS)
	$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) $(LIBS)

$(SIMPLE_PROGRAMS) : $(LIB_FILE)
$(SIMPLE_PROGRAMS) : git-%$X : %.o
	$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) \
		$(LIB_FILE) $(SIMPLE_LIB)

git-mailinfo$X: mailinfo.o $(LIB_FILE)
	$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) \
		$(LIB_FILE) $(SIMPLE_LIB) $(LIB_4_ICONV)

git-local-fetch$X: fetch.o
git-ssh-fetch$X: rsh.o fetch.o
git-ssh-upload$X: rsh.o
git-ssh-pull$X: rsh.o fetch.o
git-ssh-push$X: rsh.o

git-imap-send$X: imap-send.o $(LIB_FILE)

git-http-fetch$X: fetch.o http.o http-fetch.o $(LIB_FILE)
	$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) \
		$(LIBS) $(CURL_LIBCURL) $(EXPAT_LIBEXPAT)

git-http-push$X: revision.o http.o http-push.o $(LIB_FILE)
	$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) \
		$(LIBS) $(CURL_LIBCURL) $(EXPAT_LIBEXPAT)

init-db.o: init-db.c
	$(CC) -c $(ALL_CFLAGS) \
		-DDEFAULT_GIT_TEMPLATE_DIR='"$(template_dir_SQ)"' $*.c

$(LIB_OBJS) $(BUILTIN_OBJS): $(LIB_H)
$(patsubst git-%$X,%.o,$(PROGRAMS)): $(GITLIBS)
$(DIFF_OBJS): diffcore.h

$(LIB_FILE): $(LIB_OBJS)
	rm -f $@ && $(AR) rcs $@ $(LIB_OBJS)

XDIFF_OBJS=xdiff/xdiffi.o xdiff/xprepare.o xdiff/xutils.o xdiff/xemit.o

$(XDIFF_LIB): $(XDIFF_OBJS)
	rm -f $@ && $(AR) rcs $@ $(XDIFF_OBJS)


doc:
	$(MAKE) -C Documentation all

TAGS:
	rm -f TAGS
	find . -name '*.[hcS]' -print | xargs etags -a

tags:
	rm -f tags
	find . -name '*.[hcS]' -print | xargs ctags -a

### Testing rules

# GNU make supports exporting all variables by "export" without parameters.
# However, the environment gets quite big, and some programs have problems
# with that.

export NO_PYTHON

test: all
	$(MAKE) -C t/ all

test-date$X: test-date.c date.o ctype.o
	$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) test-date.c date.o ctype.o

test-delta$X: test-delta.c diff-delta.o patch-delta.o
	$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $^

check:
	for i in *.c; do sparse $(ALL_CFLAGS) $(SPARSE_FLAGS) $$i || exit; done



### Installation rules

install: all
	$(INSTALL) -d -m755 '$(DESTDIR_SQ)$(bindir_SQ)'
	$(INSTALL) -d -m755 '$(DESTDIR_SQ)$(gitexecdir_SQ)'
	$(INSTALL) $(ALL_PROGRAMS) '$(DESTDIR_SQ)$(gitexecdir_SQ)'
	$(INSTALL) git$X gitk '$(DESTDIR_SQ)$(bindir_SQ)'
	$(MAKE) -C templates install
	$(INSTALL) -d -m755 '$(DESTDIR_SQ)$(GIT_PYTHON_DIR_SQ)'
	$(INSTALL) $(PYMODULES) '$(DESTDIR_SQ)$(GIT_PYTHON_DIR_SQ)'
	$(foreach p,$(BUILT_INS), rm -f '$(DESTDIR_SQ)$(bindir_SQ)/$p' && ln '$(DESTDIR_SQ)$(bindir_SQ)/git$X' '$(DESTDIR_SQ)$(bindir_SQ)/$p' ;)

install-doc:
	$(MAKE) -C Documentation install




### Maintainer's dist rules

git.spec: git.spec.in
	sed -e 's/@@VERSION@@/$(GIT_VERSION)/g' < $< > $@

GIT_TARNAME=git-$(GIT_VERSION)
dist: git.spec git-tar-tree
	./git-tar-tree HEAD $(GIT_TARNAME) > $(GIT_TARNAME).tar
	@mkdir -p $(GIT_TARNAME)
	@cp git.spec $(GIT_TARNAME)
	@echo $(GIT_VERSION) > $(GIT_TARNAME)/version
	$(TAR) rf $(GIT_TARNAME).tar \
		$(GIT_TARNAME)/git.spec $(GIT_TARNAME)/version
	@rm -rf $(GIT_TARNAME)
	gzip -f -9 $(GIT_TARNAME).tar

rpm: dist
	$(RPMBUILD) -ta $(GIT_TARNAME).tar.gz

### Cleaning rules

clean:
	rm -f *.o mozilla-sha1/*.o arm/*.o ppc/*.o compat/*.o xdiff/*.o \
		$(LIB_FILE) $(XDIFF_LIB)
	rm -f $(ALL_PROGRAMS) $(BUILT_INS) git$X
	rm -f *.spec *.pyc *.pyo */*.pyc */*.pyo common-cmds.h TAGS tags
	rm -rf $(GIT_TARNAME)
	rm -f $(GIT_TARNAME).tar.gz git-core_$(GIT_VERSION)-*.tar.gz
	$(MAKE) -C Documentation/ clean
	$(MAKE) -C templates clean
	$(MAKE) -C t/ clean
	rm -f GIT-VERSION-FILE

.PHONY: all install clean strip
.PHONY: .FORCE-GIT-VERSION-FILE TAGS tags

### Check documentation
#
check-docs::
	@for v in $(ALL_PROGRAMS) $(BUILT_INS) git$X gitk; \
	do \
		case "$$v" in \
		git-merge-octopus | git-merge-ours | git-merge-recursive | \
		git-merge-resolve | git-merge-stupid | \
		git-ssh-pull | git-ssh-push ) continue ;; \
		esac ; \
		test -f "Documentation/$$v.txt" || \
		echo "no doc: $$v"; \
		grep -q "^gitlink:$$v\[[0-9]\]::" Documentation/git.txt || \
		case "$$v" in \
		git) ;; \
		*) echo "no link: $$v";; \
		esac ; \
	done | sort

