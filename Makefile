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


PROG=   update-cache show-diff init-db write-tree read-tree commit-tree \
	cat-file fsck-cache checkout-cache diff-tree rev-tree show-files \
	check-files ls-tree merge-base merge-cache unpack-file git-export \
	diff-cache convert-cache

all: $(PROG)

install: $(PROG)
	install $(PROG) $(HOME)/bin/

LIB_OBJS=read-cache.o sha1_file.o usage.o object.o commit.o tree.o blob.o
LIB_FILE=libgit.a
LIB_H=cache.h object.h

$(LIB_FILE): $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

LIBS= $(LIB_FILE) -lssl -lz

init-db: init-db.o

fsck-cache: fsck-cache.o $(LIB_FILE) object.o commit.o tree.o blob.o
	$(CC) $(CFLAGS) -o fsck-cache fsck-cache.o $(LIBS)

rev-tree: rev-tree.o $(LIB_FILE) object.o commit.o tree.o blob.o
	$(CC) $(CFLAGS) -o rev-tree rev-tree.o $(LIBS)

merge-base: merge-base.o $(LIB_FILE) object.o commit.o tree.o blob.o
	$(CC) $(CFLAGS) -o merge-base merge-base.o $(LIBS)

%: %.o $(LIB_FILE)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

blob.o: $(LIB_H)
cat-file.o: $(LIB_H)
check-files.o: $(LIB_H)
checkout-cache.o: $(LIB_H)
commit.o: $(LIB_H)
commit-tree.o: $(LIB_H)
convert-cache.o: $(LIB_H)
diff-cache.o: $(LIB_H)
diff-tree.o: $(LIB_H)
fsck-cache.o: $(LIB_H)
git-export.o: $(LIB_H)
init-db.o: $(LIB_H)
ls-tree.o: $(LIB_H)
merge-base.o: $(LIB_H)
merge-cache.o: $(LIB_H)
object.o: $(LIB_H)
read-cache.o: $(LIB_H)
read-tree.o: $(LIB_H)
rev-tree.o: $(LIB_H)
sha1_file.o: $(LIB_H)
show-diff.o: $(LIB_H)
show-files.o: $(LIB_H)
tree.o: $(LIB_H)
update-cache.o: $(LIB_H)
usage.o: $(LIB_H)
unpack-file.o: $(LIB_H)
write-tree.o: $(LIB_H)

clean:
	rm -f *.o $(PROG) $(LIB_FILE)

backup: clean
	cd .. ; tar czvf dircache.tar.gz dir-cache
