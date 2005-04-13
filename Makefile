CFLAGS=-g -O3 -Wall
CC=gcc

PROG=   update-cache show-diff init-db write-tree read-tree commit-tree \
	cat-file fsck-cache checkout-cache diff-tree rev-tree show-files \
	check-files ls-tree

all: $(PROG)

install: $(PROG)
	install $(PROG) $(HOME)/bin/

LIBS= -lssl -lz

init-db: init-db.o

update-cache: update-cache.o read-cache.o
	$(CC) $(CFLAGS) -o update-cache update-cache.o read-cache.o $(LIBS)

show-diff: show-diff.o read-cache.o
	$(CC) $(CFLAGS) -o show-diff show-diff.o read-cache.o $(LIBS)

write-tree: write-tree.o read-cache.o
	$(CC) $(CFLAGS) -o write-tree write-tree.o read-cache.o $(LIBS)

read-tree: read-tree.o read-cache.o
	$(CC) $(CFLAGS) -o read-tree read-tree.o read-cache.o $(LIBS)

commit-tree: commit-tree.o read-cache.o
	$(CC) $(CFLAGS) -o commit-tree commit-tree.o read-cache.o $(LIBS)

cat-file: cat-file.o read-cache.o
	$(CC) $(CFLAGS) -o cat-file cat-file.o read-cache.o $(LIBS)

fsck-cache: fsck-cache.o read-cache.o
	$(CC) $(CFLAGS) -o fsck-cache fsck-cache.o read-cache.o $(LIBS)

checkout-cache: checkout-cache.o read-cache.o
	$(CC) $(CFLAGS) -o checkout-cache checkout-cache.o read-cache.o $(LIBS)

diff-tree: diff-tree.o read-cache.o
	$(CC) $(CFLAGS) -o diff-tree diff-tree.o read-cache.o $(LIBS)

rev-tree: rev-tree.o read-cache.o
	$(CC) $(CFLAGS) -o rev-tree rev-tree.o read-cache.o $(LIBS)

show-files: show-files.o read-cache.o
	$(CC) $(CFLAGS) -o show-files show-files.o read-cache.o $(LIBS)

check-files: check-files.o read-cache.o
	$(CC) $(CFLAGS) -o check-files check-files.o read-cache.o $(LIBS)

ls-tree: ls-tree.o read-cache.o
	$(CC) $(CFLAGS) -o ls-tree ls-tree.o read-cache.o $(LIBS)

read-cache.o: cache.h
show-diff.o: cache.h

clean:
	rm -f *.o $(PROG) temp_git_file_*

backup: clean
	cd .. ; tar czvf dircache.tar.gz dir-cache
