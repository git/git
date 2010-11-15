#!/bin/sh

test_description='git mv in subdirs'
. ./test-lib.sh

test_expect_success \
    'prepare reference tree' \
    'mkdir path0 path1 &&
     cp "$TEST_DIRECTORY"/../COPYING path0/COPYING &&
     git add path0/COPYING &&
     git commit -m add -a'

test_expect_success \
    'moving the file out of subdirectory' \
    'cd path0 && git mv COPYING ../path1/COPYING'

# in path0 currently
test_expect_success \
    'commiting the change' \
    'cd .. && git commit -m move-out -a'

test_expect_success \
    'checking the commit' \
    'git diff-tree -r -M --name-status  HEAD^ HEAD | \
    grep "^R100..*path0/COPYING..*path1/COPYING"'

test_expect_success \
    'moving the file back into subdirectory' \
    'cd path0 && git mv ../path1/COPYING COPYING'

# in path0 currently
test_expect_success \
    'commiting the change' \
    'cd .. && git commit -m move-in -a'

test_expect_success \
    'checking the commit' \
    'git diff-tree -r -M --name-status  HEAD^ HEAD | \
    grep "^R100..*path1/COPYING..*path0/COPYING"'

test_expect_success \
    'checking -k on non-existing file' \
    'git mv -k idontexist path0'

test_expect_success \
    'checking -k on untracked file' \
    'touch untracked1 &&
     git mv -k untracked1 path0 &&
     test -f untracked1 &&
     test ! -f path0/untracked1'

test_expect_success \
    'checking -k on multiple untracked files' \
    'touch untracked2 &&
     git mv -k untracked1 untracked2 path0 &&
     test -f untracked1 &&
     test -f untracked2 &&
     test ! -f path0/untracked1 &&
     test ! -f path0/untracked2'

test_expect_success \
    'checking -f on untracked file with existing target' \
    'touch path0/untracked1 &&
     test_must_fail git mv -f untracked1 path0 &&
     test ! -f .git/index.lock &&
     test -f untracked1 &&
     test -f path0/untracked1'

# clean up the mess in case bad things happen
rm -f idontexist untracked1 untracked2 \
     path0/idontexist path0/untracked1 path0/untracked2 \
     .git/index.lock

test_expect_success \
    'adding another file' \
    'cp "$TEST_DIRECTORY"/../README path0/README &&
     git add path0/README &&
     git commit -m add2 -a'

test_expect_success \
    'moving whole subdirectory' \
    'git mv path0 path2'

test_expect_success \
    'commiting the change' \
    'git commit -m dir-move -a'

test_expect_success \
    'checking the commit' \
    'git diff-tree -r -M --name-status  HEAD^ HEAD | \
     grep "^R100..*path0/COPYING..*path2/COPYING" &&
     git diff-tree -r -M --name-status  HEAD^ HEAD | \
     grep "^R100..*path0/README..*path2/README"'

test_expect_success \
    'succeed when source is a prefix of destination' \
    'git mv path2/COPYING path2/COPYING-renamed'

test_expect_success \
    'moving whole subdirectory into subdirectory' \
    'git mv path2 path1'

test_expect_success \
    'commiting the change' \
    'git commit -m dir-move -a'

test_expect_success \
    'checking the commit' \
    'git diff-tree -r -M --name-status  HEAD^ HEAD | \
     grep "^R100..*path2/COPYING..*path1/path2/COPYING" &&
     git diff-tree -r -M --name-status  HEAD^ HEAD | \
     grep "^R100..*path2/README..*path1/path2/README"'

test_expect_success \
    'do not move directory over existing directory' \
    'mkdir path0 && mkdir path0/path2 && test_must_fail git mv path2 path0'

test_expect_success \
    'move into "."' \
    'git mv path1/path2/ .'

test_expect_success "Michael Cassar's test case" '
	rm -fr .git papers partA &&
	git init &&
	mkdir -p papers/unsorted papers/all-papers partA &&
	echo a > papers/unsorted/Thesis.pdf &&
	echo b > partA/outline.txt &&
	echo c > papers/unsorted/_another &&
	git add papers partA &&
	T1=`git write-tree` &&

	git mv papers/unsorted/Thesis.pdf papers/all-papers/moo-blah.pdf &&

	T=`git write-tree` &&
	git ls-tree -r $T | grep partA/outline.txt || {
		git ls-tree -r $T
		(exit 1)
	}
'

rm -fr papers partA path?

test_expect_success "Sergey Vlasov's test case" '
	rm -fr .git &&
	git init &&
	mkdir ab &&
	date >ab.c &&
	date >ab/d &&
	git add ab.c ab &&
	git commit -m 'initial' &&
	git mv ab a
'

test_expect_success 'absolute pathname' '(

	rm -fr mine &&
	mkdir mine &&
	cd mine &&
	test_create_repo one &&
	cd one &&
	mkdir sub &&
	>sub/file &&
	git add sub/file &&

	git mv sub "$(pwd)/in" &&
	! test -d sub &&
	test -d in &&
	git ls-files --error-unmatch in/file


)'

test_expect_success 'absolute pathname outside should fail' '(

	rm -fr mine &&
	mkdir mine &&
	cd mine &&
	out=$(pwd) &&
	test_create_repo one &&
	cd one &&
	mkdir sub &&
	>sub/file &&
	git add sub/file &&

	test_must_fail git mv sub "$out/out" &&
	test -d sub &&
	! test -d ../in &&
	git ls-files --error-unmatch sub/file

)'

test_expect_success 'git mv to move multiple sources into a directory' '
	rm -fr .git && git init &&
	mkdir dir other &&
	>dir/a.txt &&
	>dir/b.txt &&
	git add dir/?.txt &&
	git mv dir/a.txt dir/b.txt other &&
	git ls-files >actual &&
	{ echo other/a.txt; echo other/b.txt; } >expect &&
	test_cmp expect actual
'

test_expect_success 'git mv should not change sha1 of moved cache entry' '

	rm -fr .git &&
	git init &&
	echo 1 >dirty &&
	git add dirty &&
	entry="$(git ls-files --stage dirty | cut -f 1)" &&
	git mv dirty dirty2 &&
	[ "$entry" = "$(git ls-files --stage dirty2 | cut -f 1)" ] &&
	echo 2 >dirty2 &&
	git mv dirty2 dirty &&
	[ "$entry" = "$(git ls-files --stage dirty | cut -f 1)" ]

'

rm -f dirty dirty2

test_expect_success SYMLINKS 'git mv should overwrite symlink to a file' '

	rm -fr .git &&
	git init &&
	echo 1 >moved &&
	ln -s moved symlink &&
	git add moved symlink &&
	test_must_fail git mv moved symlink &&
	git mv -f moved symlink &&
	! test -e moved &&
	test -f symlink &&
	test "$(cat symlink)" = 1 &&
	git update-index --refresh &&
	git diff-files --quiet

'

rm -f moved symlink

test_expect_success SYMLINKS 'git mv should overwrite file with a symlink' '

	rm -fr .git &&
	git init &&
	echo 1 >moved &&
	ln -s moved symlink &&
	git add moved symlink &&
	test_must_fail git mv symlink moved &&
	git mv -f symlink moved &&
	! test -e symlink &&
	test -h moved &&
	git update-index --refresh &&
	git diff-files --quiet

'

rm -f moved symlink

test_done
