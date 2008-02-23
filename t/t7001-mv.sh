#!/bin/sh

test_description='git mv in subdirs'
. ./test-lib.sh

test_expect_success \
    'prepare reference tree' \
    'mkdir path0 path1 &&
     cp ../../COPYING path0/COPYING &&
     git add path0/COPYING &&
     git-commit -m add -a'

test_expect_success \
    'moving the file out of subdirectory' \
    'cd path0 && git mv COPYING ../path1/COPYING'

# in path0 currently
test_expect_success \
    'commiting the change' \
    'cd .. && git-commit -m move-out -a'

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
    'cd .. && git-commit -m move-in -a'

test_expect_success \
    'checking the commit' \
    'git diff-tree -r -M --name-status  HEAD^ HEAD | \
    grep "^R100..*path1/COPYING..*path0/COPYING"'

test_expect_success \
    'adding another file' \
    'cp ../../README path0/README &&
     git add path0/README &&
     git-commit -m add2 -a'

test_expect_success \
    'moving whole subdirectory' \
    'git mv path0 path2'

test_expect_success \
    'commiting the change' \
    'git-commit -m dir-move -a'

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
    'git-commit -m dir-move -a'

test_expect_success \
    'checking the commit' \
    'git diff-tree -r -M --name-status  HEAD^ HEAD | \
     grep "^R100..*path2/COPYING..*path1/path2/COPYING" &&
     git diff-tree -r -M --name-status  HEAD^ HEAD | \
     grep "^R100..*path2/README..*path1/path2/README"'

test_expect_success \
    'do not move directory over existing directory' \
    'mkdir path0 && mkdir path0/path2 && ! git mv path2 path0'

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

	! git mv sub "$out/out" &&
	test -d sub &&
	! test -d ../in &&
	git ls-files --error-unmatch sub/file

)'

test_done
