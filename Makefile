# The default target of this Makefile is...
all:

# Define MOZILLA_SHA1 environment variable when running make to make use of
# a bundled SHA1 routine coming from Mozilla. It is GPL'd and should be fast
# on non-x86 architectures (e.g. PowerPC), while the OpenSSL version (default
# choice) has very fast version optimized for i586.
#
# Define NO_OPENSSL environment variable if you do not have OpenSSL. You will
# miss out git-rev-list --merge-order. This also implies MOZILLA_SHA1.
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
# Define NO_STRCASESTR if you don't have strcasestr.
#
# Define NO_SETENV if you don't have setenv in the C library.
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

GIT_VERSION = 1.0.GIT

# CFLAGS and LDFLAGS are for the users to override from the command line.

CFLAGS = -g -O2 -Wall
LDFLAGS =
ALL_CFLAGS = $(CFLAGS)
ALL_LDFLAGS = $(LDFLAGS)

prefix = $(HOME)
bindir = $(prefix)/bin
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
	git-add.sh git-bisect.sh git-branch.sh git-checkout.sh \
	git-cherry.sh git-clone.sh git-commit.sh \
	git-count-objects.sh git-diff.sh git-fetch.sh \
	git-format-patch.sh git-log.sh git-ls-remote.sh \
	git-merge-one-file.sh git-parse-remote.sh \
	git-prune.sh git-pull.sh git-push.sh git-rebase.sh \
	git-repack.sh git-request-pull.sh git-reset.sh \
	git-resolve.sh git-revert.sh git-sh-setup.sh git-status.sh \
	git-tag.sh git-verify-tag.sh git-whatchanged.sh \
	git-applymbox.sh git-applypatch.sh git-am.sh \
	git-merge.sh git-merge-stupid.sh git-merge-octopus.sh \
	git-merge-resolve.sh git-merge-ours.sh git-grep.sh \
	git-lost-found.sh

SCRIPT_PERL = \
	git-archimport.perl git-cvsimport.perl git-relink.perl \
	git-shortlog.perl git-fmt-merge-msg.perl \
	git-svnimport.perl git-mv.perl git-cvsexportcommit.perl

SCRIPT_PYTHON = \
	git-merge-recursive.py

SCRIPTS = $(patsubst %.sh,%,$(SCRIPT_SH)) \
	  $(patsubst %.perl,%,$(SCRIPT_PERL)) \
	  $(patsubst %.py,%,$(SCRIPT_PYTHON)) \
	  gitk git-cherry-pick

# The ones that do not have to link with lcrypto nor lz.
SIMPLE_PROGRAMS = \
	git-get-tar-commit-id$X git-mailinfo$X git-mailsplit$X \
	git-stripspace$X git-daemon$X

# ... and all the rest
PROGRAMS = \
	git-apply$X git-cat-file$X \
	git-checkout-index$X git-clone-pack$X git-commit-tree$X \
	git-convert-objects$X git-diff-files$X \
	git-diff-index$X git-diff-stages$X \
	git-diff-tree$X git-fetch-pack$X git-fsck-objects$X \
	git-hash-object$X git-index-pack$X git-init-db$X \
	git-local-fetch$X git-ls-files$X git-ls-tree$X git-merge-base$X \
	git-merge-index$X git-mktag$X git-pack-objects$X git-patch-id$X \
	git-peek-remote$X git-prune-packed$X git-read-tree$X \
	git-receive-pack$X git-rev-list$X git-rev-parse$X \
	git-send-pack$X git-show-branch$X git-shell$X \
	git-show-index$X git-ssh-fetch$X \
	git-ssh-upload$X git-tar-tree$X git-unpack-file$X \
	git-unpack-objects$X git-update-index$X git-update-server-info$X \
	git-upload-pack$X git-verify-pack$X git-write-tree$X \
	git-update-ref$X git-symbolic-ref$X git-check-ref-format$X \
	git-name-rev$X git-pack-redundant$X git-repo-config$X git-var$X \
	git-describe$X

# what 'all' will build and 'install' will install.
ALL_PROGRAMS = $(PROGRAMS) $(SIMPLE_PROGRAMS) $(SCRIPTS) git$X

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

LIB_H = \
	blob.h cache.h commit.h count-delta.h csum-file.h delta.h \
	diff.h epoch.h object.h pack.h pkt-line.h quote.h refs.h \
	run-command.h strbuf.h tag.h tree.h git-compat-util.h

DIFF_OBJS = \
	diff.o diffcore-break.o diffcore-order.o diffcore-pathspec.o \
	diffcore-pickaxe.o diffcore-rename.o tree-diff.o

LIB_OBJS = \
	blob.o commit.o connect.o count-delta.o csum-file.o \
	date.o diff-delta.o entry.o ident.o index.o \
	object.o pack-check.o patch-delta.o path.o pkt-line.o \
	quote.o read-cache.o refs.o run-command.o \
	server-info.o setup.o sha1_file.o sha1_name.o strbuf.o \
	tag.o tree.o usage.o config.o environment.o ctype.o copy.o \
	fetch-clone.o \
	$(DIFF_OBJS)

LIBS = $(LIB_FILE)
LIBS += -lz

# Shell quote;
# Result of this needs to be placed inside ''
shq = $(subst ','\'',$(1))
# This has surrounding ''
shellquote = '$(call shq,$(1))'

#
# Platform specific tweaks
#

# We choose to avoid "if .. else if .. else .. endif endif"
# because maintaining the nesting to match is a pain.  If
# we had "elif" things would have been much nicer...
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
uname_M := $(shell sh -c 'uname -m 2>/dev/null || echo not')
uname_O := $(shell sh -c 'uname -o 2>/dev/null || echo not')
uname_R := $(shell sh -c 'uname -r 2>/dev/null || echo not')

ifeq ($(uname_S),Darwin)
	NEEDS_SSL_WITH_CRYPTO = YesPlease
	NEEDS_LIBICONV = YesPlease
	## fink
	ALL_CFLAGS += -I/sw/include
	ALL_LDFLAGS += -L/sw/lib
	## darwinports
	ALL_CFLAGS += -I/opt/local/include
	ALL_LDFLAGS += -L/opt/local/lib
endif
ifeq ($(uname_S),SunOS)
	NEEDS_SOCKET = YesPlease
	NEEDS_NSL = YesPlease
	NEEDS_LIBICONV = YesPlease
	SHELL_PATH = /bin/bash
	NO_STRCASESTR = YesPlease
	ifeq ($(uname_R),5.8)
		NO_SETENV = YesPlease
	endif
	INSTALL = ginstall
	TAR = gtar
	ALL_CFLAGS += -D__EXTENSIONS__
endif
ifeq ($(uname_O),Cygwin)
	NO_STRCASESTR = YesPlease
	NEEDS_LIBICONV = YesPlease
	# There are conflicting reports about this.
	# On some boxes NO_MMAP is needed, and not so elsewhere.
	# Try uncommenting this if you see things break -- YMMV.
	# NO_MMAP = YesPlease
	NO_IPV6 = YesPlease
	X = .exe
	ALL_CFLAGS += -DUSE_SYMLINK_HEAD=0
endif
ifeq ($(uname_S),OpenBSD)
	NO_STRCASESTR = YesPlease
	NEEDS_LIBICONV = YesPlease
	ALL_CFLAGS += -I/usr/local/include
	ALL_LDFLAGS += -L/usr/local/lib
endif
ifeq ($(uname_S),NetBSD)
	NEEDS_LIBICONV = YesPlease
	ALL_CFLAGS += -I/usr/pkg/include
	ALL_LDFLAGS += -L/usr/pkg/lib -Wl,-rpath,/usr/pkg/lib
endif
ifeq ($(uname_S),AIX)
	NO_STRCASESTR=YesPlease
	NEEDS_LIBICONV=YesPlease
endif
ifneq (,$(findstring arm,$(uname_M)))
	ARM_SHA1 = YesPlease
endif

-include config.mak

ifdef WITH_OWN_SUBPROCESS_PY
	PYMODULES += compat/subprocess.py
else
	ifneq ($(shell $(PYTHON_PATH) -c 'import subprocess;print"OK"' 2>/dev/null),OK)
		PYMODULES += compat/subprocess.py
	endif
endif

ifdef WITH_SEND_EMAIL
	SCRIPT_PERL += git-send-email.perl
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
			EXPAT_LIBEXPAT = -lexpat
			PROGRAMS += git-http-push$X
		endif
	endif
endif

ifndef NO_OPENSSL
	LIB_OBJS += epoch.o
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
ifdef NO_STRCASESTR
	COMPAT_CFLAGS += -DNO_STRCASESTR
	COMPAT_OBJS += compat/strcasestr.o
endif
ifdef NO_SETENV
	COMPAT_CFLAGS += -DNO_SETENV
	COMPAT_OBJS += compat/setenv.o
endif
ifdef NO_MMAP
	COMPAT_CFLAGS += -DNO_MMAP
	COMPAT_OBJS += compat/mmap.o
endif
ifdef NO_IPV6
	ALL_CFLAGS += -DNO_IPV6 -Dsockaddr_storage=sockaddr_in
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

ALL_CFLAGS += -DSHA1_HEADER=$(call shellquote,$(SHA1_HEADER)) $(COMPAT_CFLAGS)
LIB_OBJS += $(COMPAT_OBJS)
export prefix TAR INSTALL DESTDIR SHELL_PATH template_dir
### Build rules

all: $(ALL_PROGRAMS)

all:
	$(MAKE) -C templates

git$X: git.c $(LIB_FILE) Makefile
	$(CC) -DGIT_EXEC_PATH='"$(bindir)"' -DGIT_VERSION='"$(GIT_VERSION)"' \
		$(CFLAGS) $(COMPAT_CFLAGS) -o $@ $(filter %.c,$^) $(LIB_FILE)

$(patsubst %.sh,%,$(SCRIPT_SH)) : % : %.sh
	rm -f $@
	sed -e '1s|#!.*/sh|#!$(call shq,$(SHELL_PATH))|' \
	    -e 's/@@GIT_VERSION@@/$(GIT_VERSION)/g' \
	    $@.sh >$@
	chmod +x $@

$(patsubst %.perl,%,$(SCRIPT_PERL)) : % : %.perl
	rm -f $@
	sed -e '1s|#!.*perl|#!$(call shq,$(PERL_PATH))|' \
	    -e 's/@@GIT_VERSION@@/$(GIT_VERSION)/g' \
	    $@.perl >$@
	chmod +x $@

$(patsubst %.py,%,$(SCRIPT_PYTHON)) : % : %.py
	rm -f $@
	sed -e '1s|#!.*python|#!$(call shq,$(PYTHON_PATH))|' \
	    -e 's|@@GIT_PYTHON_PATH@@|$(call shq,$(GIT_PYTHON_DIR))|g' \
	    -e 's/@@GIT_VERSION@@/$(GIT_VERSION)/g' \
	    $@.py >$@
	chmod +x $@

git-cherry-pick: git-revert
	cp $< $@

# format-patch records GIT_VERSION
git-format-patch: Makefile

%.o: %.c
	$(CC) -o $*.o -c $(ALL_CFLAGS) $<
%.o: %.S
	$(CC) -o $*.o -c $(ALL_CFLAGS) $<

git-%$X: %.o $(LIB_FILE)
	$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) $(LIBS)

git-mailinfo$X : SIMPLE_LIB += $(LIB_4_ICONV)
$(SIMPLE_PROGRAMS) : $(LIB_FILE)
$(SIMPLE_PROGRAMS) : git-%$X : %.o
	$(CC) $(ALL_CFLAGS) -o $@ $(ALL_LDFLAGS) $(filter %.o,$^) \
		$(LIB_FILE) $(SIMPLE_LIB)

git-http-fetch$X: fetch.o http.o
git-http-push$X: http.o
git-local-fetch$X: fetch.o
git-ssh-fetch$X: rsh.o fetch.o
git-ssh-upload$X: rsh.o
git-ssh-pull$X: rsh.o fetch.o
git-ssh-push$X: rsh.o

git-http-fetch$X: LIBS += $(CURL_LIBCURL)
git-http-push$X: LIBS += $(CURL_LIBCURL) $(EXPAT_LIBEXPAT)
git-rev-list$X: LIBS += $(OPENSSL_LIBSSL)

init-db.o: init-db.c
	$(CC) -c $(ALL_CFLAGS) \
		-DDEFAULT_GIT_TEMPLATE_DIR=$(call shellquote,"$(template_dir)") $*.c

$(LIB_OBJS): $(LIB_H)
$(patsubst git-%$X,%.o,$(PROGRAMS)): $(LIB_H)
$(DIFF_OBJS): diffcore.h

$(LIB_FILE): $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

doc:
	$(MAKE) -C Documentation all


### Testing rules

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
	$(INSTALL) -d -m755 $(call shellquote,$(DESTDIR)$(bindir))
	$(INSTALL) $(ALL_PROGRAMS) $(call shellquote,$(DESTDIR)$(bindir))
	$(MAKE) -C templates install
	$(INSTALL) -d -m755 $(call shellquote,$(DESTDIR)$(GIT_PYTHON_DIR))
	$(INSTALL) $(PYMODULES) $(call shellquote,$(DESTDIR)$(GIT_PYTHON_DIR))

install-doc:
	$(MAKE) -C Documentation install




### Maintainer's dist rules

git.spec: git.spec.in Makefile
	sed -e 's/@@VERSION@@/$(GIT_VERSION)/g' < $< > $@

GIT_TARNAME=git-$(GIT_VERSION)
dist: git.spec git-tar-tree
	./git-tar-tree HEAD $(GIT_TARNAME) > $(GIT_TARNAME).tar
	@mkdir -p $(GIT_TARNAME)
	@cp git.spec $(GIT_TARNAME)
	$(TAR) rf $(GIT_TARNAME).tar $(GIT_TARNAME)/git.spec
	@rm -rf $(GIT_TARNAME)
	gzip -f -9 $(GIT_TARNAME).tar

rpm: dist
	$(RPMBUILD) -ta $(GIT_TARNAME).tar.gz

deb: dist
	rm -rf $(GIT_TARNAME)
	$(TAR) zxf $(GIT_TARNAME).tar.gz
	dpkg-source -b $(GIT_TARNAME)
	cd $(GIT_TARNAME) && fakeroot debian/rules binary

### Cleaning rules

clean:
	rm -f *.o mozilla-sha1/*.o arm/*.o ppc/*.o compat/*.o $(LIB_FILE)
	rm -f $(PROGRAMS) $(SIMPLE_PROGRAMS) git$X
	rm -f $(filter-out gitk,$(SCRIPTS))
	rm -f *.spec *.pyc *.pyo */*.pyc */*.pyo
	rm -rf $(GIT_TARNAME)
	rm -f $(GIT_TARNAME).tar.gz git-core_$(GIT_VERSION)-*.tar.gz
	rm -f git-core_$(GIT_VERSION)-*.dsc
	rm -f git-*_$(GIT_VERSION)-*.deb
	$(MAKE) -C Documentation/ clean
	$(MAKE) -C templates clean
	$(MAKE) -C t/ clean

