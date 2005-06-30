# -DCOLLISION_CHECK if you believe that SHA1's
# 1461501637330902918203684832716283019655932542976 hashes do not give you
# enough guarantees about no collisions between objects ever hapenning.
#
# -DUSE_NSEC if you want git to care about sub-second file mtimes and ctimes.
# -DUSE_STDEV if you want git to care about st_dev changing
#
# Note that you need some new glibc (at least >2.2.4) for this, and it will
# BREAK YOUR LOCAL DIFFS! show-diff and anything using it will likely randomly
# break unless your underlying filesystem supports those sub-second times
# (my ext3 doesn't).
COPTS=
CFLAGS=-g $(COPTS) -Wall

prefix=$(HOME)
bin=$(prefix)/bin
# dest=

CC=gcc
AR=ar
INSTALL=install

SCRIPTS=git git-apply-patch-script git-merge-one-file-script git-prune-script \
	git-pull-script git-tag-script git-resolve-script git-whatchanged \
	git-fetch-script git-status-script git-commit-script \
	git-log-script git-shortlog git-cvsimport-script git-diff-script \
	git-reset-script git-add-script git-checkout-script git-clone-script \
	gitk git-cherry git-rebase-script git-relink-script

PROG=   git-update-cache git-diff-files git-init-db git-write-tree \
	git-read-tree git-commit-tree git-cat-file git-fsck-cache \
	git-checkout-cache git-diff-tree git-rev-tree git-ls-files \
	git-check-files git-ls-tree git-merge-base git-merge-cache \
	git-unpack-file git-export git-diff-cache git-convert-cache \
	git-http-pull git-ssh-push git-ssh-pull git-rev-list git-mktag \
	git-diff-helper git-tar-tree git-local-pull git-write-blob \
	git-get-tar-commit-id git-apply git-stripspace \
	git-cvs2git git-diff-stages git-rev-parse git-patch-id \
	git-pack-objects git-unpack-objects git-verify-pack \
	git-receive-pack git-send-pack

all: $(PROG)

install: $(PROG) $(SCRIPTS)
	$(INSTALL) $(PROG) $(SCRIPTS) $(dest)$(bin)

LIB_OBJS=read-cache.o sha1_file.o usage.o object.o commit.o tree.o blob.o \
	 tag.o date.o index.o diff-delta.o patch-delta.o entry.o \
	 epoch.o refs.o csum-file.o verify_pack.o pkt-line.o
LIB_FILE=libgit.a
LIB_H=cache.h object.h blob.h tree.h commit.h tag.h delta.h epoch.h csum-file.h \
	pack.h pkt-line.h

LIB_H += strbuf.h
LIB_OBJS += strbuf.o

LIB_H += diff.h count-delta.h
LIB_OBJS += diff.o diffcore-rename.o diffcore-pickaxe.o diffcore-pathspec.o \
	count-delta.o diffcore-break.o diffcore-order.o

LIB_OBJS += gitenv.o

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
  LIBS += -lcrypto
endif
endif

CFLAGS += '-DSHA1_HEADER=$(SHA1_HEADER)'

$(LIB_FILE): $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

test-date: test-date.c date.o
	$(CC) $(CFLAGS) -o $@ test-date.c date.o

test-delta: test-delta.c diff-delta.o patch-delta.o
	$(CC) $(CFLAGS) -o $@ $^

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
git-ls-files: ls-files.c
git-check-files: check-files.c
git-ls-tree: ls-tree.c
git-merge-base: merge-base.c
git-merge-cache: merge-cache.c
git-unpack-file: unpack-file.c
git-export: export.c
git-diff-cache: diff-cache.c
git-convert-cache: convert-cache.c
git-http-pull: http-pull.c pull.c
git-local-pull: local-pull.c pull.c
git-ssh-push: rsh.c
git-ssh-pull: rsh.c pull.c
git-rev-list: rev-list.c
git-mktag: mktag.c
git-diff-helper: diff-helper.c
git-tar-tree: tar-tree.c
git-write-blob: write-blob.c
git-stripspace: stripspace.c
git-cvs2git: cvs2git.c
git-diff-stages: diff-stages.c
git-rev-parse: rev-parse.c
git-patch-id: patch-id.c
git-pack-objects: pack-objects.c
git-unpack-objects: unpack-objects.c
git-verify-pack: verify-pack.c
git-receive-pack: receive-pack.c
git-send-pack: send-pack.c

git-http-pull: LIBS += -lcurl
git-rev-list: LIBS += -lssl

# Library objects..
blob.o: $(LIB_H)
tree.o: $(LIB_H)
commit.o: $(LIB_H)
tag.o: $(LIB_H)
object.o: $(LIB_H)
read-cache.o: $(LIB_H)
sha1_file.o: $(LIB_H)
usage.o: $(LIB_H)
strbuf.o: $(LIB_H)
gitenv.o: $(LIB_H)
entry.o: $(LIB_H)
diff.o: $(LIB_H) diffcore.h
diffcore-rename.o : $(LIB_H) diffcore.h
diffcore-pathspec.o : $(LIB_H) diffcore.h
diffcore-pickaxe.o : $(LIB_H) diffcore.h
diffcore-break.o : $(LIB_H) diffcore.h
diffcore-order.o : $(LIB_H) diffcore.h
epoch.o: $(LIB_H)

test: all
	$(MAKE) -C t/ all

clean:
	rm -f *.o mozilla-sha1/*.o ppc/*.o $(PROG) $(LIB_FILE)
	$(MAKE) -C Documentation/ clean

backup: clean
	cd .. ; tar czvf dircache.tar.gz dir-cache
