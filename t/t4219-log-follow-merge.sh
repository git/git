#!/bin/sh

test_description='Test --follow follows renames across merges'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=master
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup subtree-merged repository' '
	git init inner &&
	echo inner >inner/inner.txt &&
	git -C inner add inner.txt &&
	git -C inner commit -m "inner init" &&

	git init outer &&
	echo outer >outer/outer.txt &&
	git -C outer add outer.txt &&
	git -C outer commit -m "outer init" &&

	git -C outer fetch ../inner master &&
	git -C outer merge -s ours --no-commit --allow-unrelated-histories \
		FETCH_HEAD &&
	git -C outer read-tree --prefix=inner/ -u FETCH_HEAD &&
	git -C outer commit -m "Merge inner repo into inner/ subdirectory"
'

test_expect_success '--follow finds the pre-merge commit through a subtree merge' '
	git -C outer log --follow --pretty=tformat:%s inner/inner.txt >actual &&
	echo "inner init" >expect &&
	test_cmp expect actual
'

test_expect_success 'setup merge of two branches that both renamed a file to README' '
	git init foo &&
	mkdir foo/foo &&
	echo "foo readme" >foo/foo/README &&
	git -C foo add foo/README &&
	git -C foo commit -m "add foo README" &&

	git -C foo mv foo/README README &&
	git -C foo commit -m "promote foo README to toplevel" &&

	echo "foo c" >foo/foo.c &&
	git -C foo add foo.c &&
	git -C foo commit -m "add foo C impl" &&

	git init bar &&
	mkdir bar/bar &&
	echo "bar readme" >bar/bar/README &&
	git -C bar add bar/README &&
	git -C bar commit -m "add bar README" &&

	git -C bar mv bar/README README &&
	git -C bar commit -m "promote bar README to toplevel" &&

	echo "bar c" >bar/bar.c &&
	git -C bar add bar.c &&
	git -C bar commit -m "add bar C impl" &&

	git -C foo fetch ../bar master &&
	git -C foo merge -s ours --no-commit --allow-unrelated-histories \
		FETCH_HEAD &&
	git -C foo checkout FETCH_HEAD -- bar.c &&
	git -C foo commit -m "merge bar into foo"
'

test_expect_success '--follow follows renames across both sides of a merge' '
	git -C foo log --follow --pretty=tformat:%s README >actual &&
	sort actual >actual.sorted &&
	cat >expect <<-\EOF &&
	add bar README
	add foo README
	promote bar README to toplevel
	promote foo README to toplevel
	EOF
	test_cmp expect actual.sorted
'

test_expect_success 'setup diamond with renames on both sides of a fork' '
	git init diamond &&
	test_lines="line 1\nline 2\nline 3\nline 4\nline 5\n" &&

	printf "$test_lines" >diamond/path0 &&
	git -C diamond add path0 &&
	git -C diamond commit -m "A: add path0" &&

	git -C diamond checkout -b upper &&
	printf "line 1\nline 2\nline 3 modified by B\nline 4\nline 5\n" \
		>diamond/path0 &&
	git -C diamond commit -am "B: modify path0 on upper" &&
	git -C diamond mv path0 path1 &&
	git -C diamond commit -m "X: rename path0 to path1" &&

	git -C diamond checkout -b lower master &&
	printf "line 1\nline 2\nline 3 modified by C\nline 4\nline 5\n" \
		>diamond/path0 &&
	git -C diamond commit -am "C: modify path0 on lower" &&
	git -C diamond mv path0 path2 &&
	git -C diamond commit -m "Y: rename path0 to path2" &&

	git -C diamond checkout upper &&
	git -C diamond merge -s ours --no-commit lower &&
	git -C diamond rm path1 &&
	printf "line 1\nline 2\nline 3 merged\nline 4\nline 5\n" \
		>diamond/path &&
	git -C diamond add path &&
	git -C diamond commit -m "M: merge with rename to path" &&

	printf "line 1\nline 2\nline 3 merged again\nline 4\nline 5\n" \
		>diamond/path &&
	git -C diamond commit -am "Z: modify path"
'

test_expect_success '--follow follows renames through a fork in a single history' '
	git -C diamond log --follow --pretty=tformat:%s path >actual &&
	sort actual >actual.sorted &&
	cat >expect <<-\EOF &&
	A: add path0
	B: modify path0 on upper
	C: modify path0 on lower
	X: rename path0 to path1
	Y: rename path0 to path2
	Z: modify path
	EOF
	test_cmp expect actual.sorted
'

test_done
