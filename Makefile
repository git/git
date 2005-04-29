# -DCOLLISION_CHECK if you believe that SHA1's
# 1461501637330902918203684832716283019655932542976 hashes do not give you
# enough guarantees about no collisions between objects ever hapenning.
#
# -DNSEC if you want git to care about sub-second file mtimes and ctimes.
# Note that you need some new glibc (at least >2.2.4) for this, and it will
# BREAK YOUR LOCAL DIFFS! show-diff and anything using it will likely randomly
# break unless your underlying filesystem supports those sub-second times
# (my ext3 doesn't).
CFLAGS=-g -O2 -Wall

CC=gcc
AR=ar

SCRIPTS=git-merge-one-file-script git-prune-script git-pull-script \
	git-tag-script

PROG=   git-update-cache git-diff-files git-init-db git-write-tree \
	git-read-tree git-commit-tree git-cat-file git-fsck-cache \
	git-checkout-cache git-diff-tree git-rev-tree git-show-files \
	git-check-files git-ls-tree git-merge-base git-merge-cache \
	git-unpack-file git-export git-diff-cache git-convert-cache \
	git-http-pull git-rpush git-rpull git-rev-list git-mktag \
	git-diff-tree-helper git-tar-tree

all: $(PROG)

install: $(PROG) $(SCRIPTS)
	install $(PROG) $(SCRIPTS) $(HOME)/bin/

LIB_OBJS=read-cache.o sha1_file.o usage.o object.o commit.o tree.o blob.o tag.o
LIB_FILE=libgit.a
LIB_H=cache.h object.h blob.h tree.h commit.h tag.h

LIB_H += strbuf.h
LIB_OBJS += strbuf.o

LIB_H += diff.h
LIB_OBJS += diff.o

LIBS = $(LIB_FILE)
LIBS += -lz

ifdef MOZILLA_SHA1
  SHA1_HEADER="mozilla-sha1/sha1.h"
  LIB_OBJS += mozilla-sha1/sha1.o
else
ifdef PPC_SHA1
  SHA1_HEADER="ppc/sha1.h"
  LIB_OBJS += ppc/sha1.o ppc/sha1ppc.o
else
  SHA1_HEADER=<openssl/sha.h>
  LIBS += -lssl
endif
endif

CFLAGS += '-DSHA1_HEADER=$(SHA1_HEADER)'

$(LIB_FILE): $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

init-db: init-db.o

git-%: %.c $(LIB_FILE)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^) $(LIBS)

git-update-cache: update-cache.c
git-diff-files: diff-files.c
git-init-db: init-db.c
git-write-tree: write-tree.c
git-read-tree: read-tree.c
git-commit-tree: commit-tree.c
git-cat-file: cat-file.c
git-fsck-cache: fsck-cache.c
git-checkout-cache: checkout-cache.c
git-diff-tree: diff-tree.c
git-rev-tree: rev-tree.c
git-show-files: show-files.c
git-check-files: check-files.c
git-ls-tree: ls-tree.c
git-merge-base: merge-base.c
git-merge-cache: merge-cache.c
git-unpack-file: unpack-file.c
git-export: export.c
git-diff-cache: diff-cache.c
git-convert-cache: convert-cache.c
git-http-pull: http-pull.c
git-rpush: rsh.c
git-rpull: rsh.c
git-rev-list: rev-list.c
git-mktag: mktag.c
git-diff-tree-helper: diff-tree-helper.c
git-tar-tree: tar-tree.c

git-http-pull: LIBS += -lcurl

# Library objects..
blob.o: $(LIB_H)
tree.o: $(LIB_H)
commit.o: $(LIB_H)
tag.o: $(LIB_H)
object.o: $(LIB_H)
read-cache.o: $(LIB_H)
sha1_file.o: $(LIB_H)
usage.o: $(LIB_H)
diff.o: $(LIB_H)

clean:
	rm -f *.o mozilla-sha1/*.o ppc/*.o $(PROG) $(LIB_FILE)

backup: clean
	cd .. ; tar czvf dircache.tar.gz dir-cache
