#!/bin/sh

test_description='merge-recursive backend test'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-merge.sh

test_expect_success 'setup 1' '

	echo hello >a &&
	o0=$(git hash-object a) &&
	cp a b &&
	cp a c &&
	mkdir d &&
	cp a d/e &&

	test_tick &&
	git add a b c d/e &&
	git commit -m initial &&
	c0=$(git rev-parse --verify HEAD) &&
	git branch side &&
	git branch df-1 &&
	git branch df-2 &&
	git branch df-3 &&
	git branch remove &&
	git branch submod &&
	git branch copy &&
	git branch rename &&
	git branch rename-ln &&

	echo hello >>a &&
	cp a d/e &&
	o1=$(git hash-object a) &&

	git add a d/e &&

	test_tick &&
	git commit -m "main modifies a and d/e" &&
	c1=$(git rev-parse --verify HEAD) &&
	( git ls-tree -r HEAD && git ls-files -s ) >actual &&
	(
		echo "100644 blob $o1	a" &&
		echo "100644 blob $o0	b" &&
		echo "100644 blob $o0	c" &&
		echo "100644 blob $o1	d/e" &&
		echo "100644 $o1 0	a" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o1 0	d/e"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'setup 2' '

	rm -rf [abcd] &&
	git checkout side &&
	( git ls-tree -r HEAD && git ls-files -s ) >actual &&
	(
		echo "100644 blob $o0	a" &&
		echo "100644 blob $o0	b" &&
		echo "100644 blob $o0	c" &&
		echo "100644 blob $o0	d/e" &&
		echo "100644 $o0 0	a" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o0 0	d/e"
	) >expected &&
	test_cmp expected actual &&

	echo goodbye >>a &&
	o2=$(git hash-object a) &&

	git add a &&

	test_tick &&
	git commit -m "side modifies a" &&
	c2=$(git rev-parse --verify HEAD) &&
	( git ls-tree -r HEAD && git ls-files -s ) >actual &&
	(
		echo "100644 blob $o2	a" &&
		echo "100644 blob $o0	b" &&
		echo "100644 blob $o0	c" &&
		echo "100644 blob $o0	d/e" &&
		echo "100644 $o2 0	a" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o0 0	d/e"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'setup 3' '

	rm -rf [abcd] &&
	git checkout df-1 &&
	( git ls-tree -r HEAD && git ls-files -s ) >actual &&
	(
		echo "100644 blob $o0	a" &&
		echo "100644 blob $o0	b" &&
		echo "100644 blob $o0	c" &&
		echo "100644 blob $o0	d/e" &&
		echo "100644 $o0 0	a" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o0 0	d/e"
	) >expected &&
	test_cmp expected actual &&

	rm -f b && mkdir b && echo df-1 >b/c && git add b/c &&
	o3=$(git hash-object b/c) &&

	test_tick &&
	git commit -m "df-1 makes b/c" &&
	c3=$(git rev-parse --verify HEAD) &&
	( git ls-tree -r HEAD && git ls-files -s ) >actual &&
	(
		echo "100644 blob $o0	a" &&
		echo "100644 blob $o3	b/c" &&
		echo "100644 blob $o0	c" &&
		echo "100644 blob $o0	d/e" &&
		echo "100644 $o0 0	a" &&
		echo "100644 $o3 0	b/c" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o0 0	d/e"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'setup 4' '

	rm -rf [abcd] &&
	git checkout df-2 &&
	( git ls-tree -r HEAD && git ls-files -s ) >actual &&
	(
		echo "100644 blob $o0	a" &&
		echo "100644 blob $o0	b" &&
		echo "100644 blob $o0	c" &&
		echo "100644 blob $o0	d/e" &&
		echo "100644 $o0 0	a" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o0 0	d/e"
	) >expected &&
	test_cmp expected actual &&

	rm -f a && mkdir a && echo df-2 >a/c && git add a/c &&
	o4=$(git hash-object a/c) &&

	test_tick &&
	git commit -m "df-2 makes a/c" &&
	c4=$(git rev-parse --verify HEAD) &&
	( git ls-tree -r HEAD && git ls-files -s ) >actual &&
	(
		echo "100644 blob $o4	a/c" &&
		echo "100644 blob $o0	b" &&
		echo "100644 blob $o0	c" &&
		echo "100644 blob $o0	d/e" &&
		echo "100644 $o4 0	a/c" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o0 0	d/e"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'setup 5' '

	rm -rf [abcd] &&
	git checkout remove &&
	( git ls-tree -r HEAD && git ls-files -s ) >actual &&
	(
		echo "100644 blob $o0	a" &&
		echo "100644 blob $o0	b" &&
		echo "100644 blob $o0	c" &&
		echo "100644 blob $o0	d/e" &&
		echo "100644 $o0 0	a" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o0 0	d/e"
	) >expected &&
	test_cmp expected actual &&

	rm -f b &&
	echo remove-conflict >a &&

	git add a &&
	git rm b &&
	o5=$(git hash-object a) &&

	test_tick &&
	git commit -m "remove removes b and modifies a" &&
	c5=$(git rev-parse --verify HEAD) &&
	( git ls-tree -r HEAD && git ls-files -s ) >actual &&
	(
		echo "100644 blob $o5	a" &&
		echo "100644 blob $o0	c" &&
		echo "100644 blob $o0	d/e" &&
		echo "100644 $o5 0	a" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o0 0	d/e"
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'setup 6' '

	rm -rf [abcd] &&
	git checkout df-3 &&
	( git ls-tree -r HEAD && git ls-files -s ) >actual &&
	(
		echo "100644 blob $o0	a" &&
		echo "100644 blob $o0	b" &&
		echo "100644 blob $o0	c" &&
		echo "100644 blob $o0	d/e" &&
		echo "100644 $o0 0	a" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o0 0	d/e"
	) >expected &&
	test_cmp expected actual &&

	rm -fr d && echo df-3 >d && git add d &&
	o6=$(git hash-object d) &&

	test_tick &&
	git commit -m "df-3 makes d" &&
	c6=$(git rev-parse --verify HEAD) &&
	( git ls-tree -r HEAD && git ls-files -s ) >actual &&
	(
		echo "100644 blob $o0	a" &&
		echo "100644 blob $o0	b" &&
		echo "100644 blob $o0	c" &&
		echo "100644 blob $o6	d" &&
		echo "100644 $o0 0	a" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o6 0	d"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'setup 7' '

	git checkout submod &&
	git rm d/e &&
	test_tick &&
	git commit -m "remove d/e" &&
	git update-index --add --cacheinfo 160000 $c1 d &&
	test_tick &&
	git commit -m "make d/ a submodule"
'

test_expect_success 'setup 8' '
	git checkout rename &&
	git mv a e &&
	git add e &&
	test_tick &&
	git commit -m "rename a->e" &&
	c7=$(git rev-parse --verify HEAD) &&
	git checkout rename-ln &&
	git mv a e &&
	test_ln_s_add e a &&
	test_tick &&
	git commit -m "rename a->e, symlink a->e" &&
	oln=$(printf e | git hash-object --stdin)
'

test_expect_success 'setup 9' '
	git checkout copy &&
	cp a e &&
	git add e &&
	test_tick &&
	git commit -m "copy a->e"
'

test_expect_success 'merge-recursive simple' '

	rm -fr [abcd] &&
	git checkout -f "$c2" &&

	test_expect_code 1 git merge-recursive "$c0" -- "$c2" "$c1"
'

test_expect_success 'merge-recursive result' '

	git ls-files -s >actual &&
	(
		echo "100644 $o0 1	a" &&
		echo "100644 $o2 2	a" &&
		echo "100644 $o1 3	a" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o1 0	d/e"
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'fail if the index has unresolved entries' '

	rm -fr [abcd] &&
	git checkout -f "$c1" &&

	test_must_fail git merge "$c5" &&
	test_must_fail git merge "$c5" 2> out &&
	test_grep "not possible because you have unmerged files" out &&
	git add -u &&
	test_must_fail git merge "$c5" 2> out &&
	test_grep "You have not concluded your merge" out &&
	rm -f .git/MERGE_HEAD &&
	test_must_fail git merge "$c5" 2> out &&
	test_grep "Your local changes to the following files would be overwritten by merge:" out
'

test_expect_success 'merge-recursive remove conflict' '

	rm -fr [abcd] &&
	git checkout -f "$c1" &&

	test_expect_code 1 git merge-recursive "$c0" -- "$c1" "$c5"
'

test_expect_success 'merge-recursive remove conflict' '

	git ls-files -s >actual &&
	(
		echo "100644 $o0 1	a" &&
		echo "100644 $o1 2	a" &&
		echo "100644 $o5 3	a" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o1 0	d/e"
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'merge-recursive d/f simple' '
	rm -fr [abcd] &&
	git reset --hard &&
	git checkout -f "$c1" &&

	git merge-recursive "$c0" -- "$c1" "$c3"
'

test_expect_success 'merge-recursive result' '

	git ls-files -s >actual &&
	(
		echo "100644 $o1 0	a" &&
		echo "100644 $o3 0	b/c" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o1 0	d/e"
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'merge-recursive d/f conflict' '

	rm -fr [abcd] &&
	git reset --hard &&
	git checkout -f "$c1" &&

	test_expect_code 1 git merge-recursive "$c0" -- "$c1" "$c4"
'

test_expect_success 'merge-recursive d/f conflict result' '

	git ls-files -s >actual &&
	(
		echo "100644 $o0 1	a" &&
		echo "100644 $o1 2	a" &&
		echo "100644 $o4 0	a/c" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o1 0	d/e"
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'merge-recursive d/f conflict the other way' '

	rm -fr [abcd] &&
	git reset --hard &&
	git checkout -f "$c4" &&

	test_expect_code 1 git merge-recursive "$c0" -- "$c4" "$c1"
'

test_expect_success 'merge-recursive d/f conflict result the other way' '

	git ls-files -s >actual &&
	(
		echo "100644 $o0 1	a" &&
		echo "100644 $o1 3	a" &&
		echo "100644 $o4 0	a/c" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o1 0	d/e"
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'merge-recursive d/f conflict' '

	rm -fr [abcd] &&
	git reset --hard &&
	git checkout -f "$c1" &&

	test_expect_code 1 git merge-recursive "$c0" -- "$c1" "$c6"
'

test_expect_success 'merge-recursive d/f conflict result' '

	git ls-files -s >actual &&
	(
		echo "100644 $o1 0	a" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o6 3	d" &&
		echo "100644 $o0 1	d/e" &&
		echo "100644 $o1 2	d/e"
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'merge-recursive d/f conflict' '

	rm -fr [abcd] &&
	git reset --hard &&
	git checkout -f "$c6" &&

	test_expect_code 1 git merge-recursive "$c0" -- "$c6" "$c1"
'

test_expect_success 'merge-recursive d/f conflict result' '

	git ls-files -s >actual &&
	(
		echo "100644 $o1 0	a" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o6 2	d" &&
		echo "100644 $o0 1	d/e" &&
		echo "100644 $o1 3	d/e"
	) >expected &&
	test_cmp expected actual

'

test_expect_success SYMLINKS 'dir in working tree with symlink ancestor does not produce d/f conflict' '
	git init sym &&
	(
		cd sym &&
		ln -s . foo &&
		mkdir bar &&
		>bar/file &&
		git add foo bar/file &&
		git commit -m "foo symlink" &&

		git checkout -b branch1 &&
		git commit --allow-empty -m "empty commit" &&

		git checkout main &&
		git rm foo &&
		mkdir foo &&
		>foo/bar &&
		git add foo/bar &&
		git commit -m "replace foo symlink with real foo dir and foo/bar file" &&

		git checkout branch1 &&

		git cherry-pick main &&
		test_path_is_dir foo &&
		test_path_is_file foo/bar
	)
'

test_expect_success 'reset and 3-way merge' '

	git reset --hard "$c2" &&
	git read-tree -m "$c0" "$c2" "$c1"

'

test_expect_success 'reset and bind merge' '

	git reset --hard main &&
	git read-tree --prefix=M/ main &&
	git ls-files -s >actual &&
	(
		echo "100644 $o1 0	M/a" &&
		echo "100644 $o0 0	M/b" &&
		echo "100644 $o0 0	M/c" &&
		echo "100644 $o1 0	M/d/e" &&
		echo "100644 $o1 0	a" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o1 0	d/e"
	) >expected &&
	test_cmp expected actual &&

	git read-tree --prefix=a1/ main &&
	git ls-files -s >actual &&
	(
		echo "100644 $o1 0	M/a" &&
		echo "100644 $o0 0	M/b" &&
		echo "100644 $o0 0	M/c" &&
		echo "100644 $o1 0	M/d/e" &&
		echo "100644 $o1 0	a" &&
		echo "100644 $o1 0	a1/a" &&
		echo "100644 $o0 0	a1/b" &&
		echo "100644 $o0 0	a1/c" &&
		echo "100644 $o1 0	a1/d/e" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o1 0	d/e"
	) >expected &&
	test_cmp expected actual &&

	git read-tree --prefix=z/ main &&
	git ls-files -s >actual &&
	(
		echo "100644 $o1 0	M/a" &&
		echo "100644 $o0 0	M/b" &&
		echo "100644 $o0 0	M/c" &&
		echo "100644 $o1 0	M/d/e" &&
		echo "100644 $o1 0	a" &&
		echo "100644 $o1 0	a1/a" &&
		echo "100644 $o0 0	a1/b" &&
		echo "100644 $o0 0	a1/c" &&
		echo "100644 $o1 0	a1/d/e" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o1 0	d/e" &&
		echo "100644 $o1 0	z/a" &&
		echo "100644 $o0 0	z/b" &&
		echo "100644 $o0 0	z/c" &&
		echo "100644 $o1 0	z/d/e"
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'merge-recursive w/ empty work tree - ours has rename' '
	(
		GIT_WORK_TREE="$PWD/ours-has-rename-work" &&
		export GIT_WORK_TREE &&
		GIT_INDEX_FILE="$PWD/ours-has-rename-index" &&
		export GIT_INDEX_FILE &&
		mkdir "$GIT_WORK_TREE" &&
		git read-tree -i -m $c7 2>actual-err &&
		test_must_be_empty actual-err &&
		git update-index --ignore-missing --refresh 2>actual-err &&
		test_must_be_empty actual-err &&
		git merge-recursive $c0 -- $c7 $c3 2>actual-err &&
		test_must_be_empty actual-err &&
		git ls-files -s >actual-files 2>actual-err &&
		test_must_be_empty actual-err
	) &&
	cat >expected-files <<-EOF &&
	100644 $o3 0	b/c
	100644 $o0 0	c
	100644 $o0 0	d/e
	100644 $o0 0	e
	EOF
	test_cmp expected-files actual-files
'

test_expect_success 'merge-recursive w/ empty work tree - theirs has rename' '
	(
		GIT_WORK_TREE="$PWD/theirs-has-rename-work" &&
		export GIT_WORK_TREE &&
		GIT_INDEX_FILE="$PWD/theirs-has-rename-index" &&
		export GIT_INDEX_FILE &&
		mkdir "$GIT_WORK_TREE" &&
		git read-tree -i -m $c3 2>actual-err &&
		test_must_be_empty actual-err &&
		git update-index --ignore-missing --refresh 2>actual-err &&
		test_must_be_empty actual-err &&
		git merge-recursive $c0 -- $c3 $c7 2>actual-err &&
		test_must_be_empty actual-err &&
		git ls-files -s >actual-files 2>actual-err &&
		test_must_be_empty actual-err
	) &&
	cat >expected-files <<-EOF &&
	100644 $o3 0	b/c
	100644 $o0 0	c
	100644 $o0 0	d/e
	100644 $o0 0	e
	EOF
	test_cmp expected-files actual-files
'

test_expect_success 'merge removes empty directories' '

	git reset --hard main &&
	git checkout -b rm &&
	git rm d/e &&
	git commit -mremoved-d/e &&
	git checkout main &&
	git merge -s recursive rm &&
	test_path_is_missing d
'

test_expect_success 'merge-recursive simple w/submodule' '

	git checkout submod &&
	git merge remove
'

test_expect_success 'merge-recursive simple w/submodule result' '

	git ls-files -s >actual &&
	(
		echo "100644 $o5 0	a" &&
		echo "100644 $o0 0	c" &&
		echo "160000 $c1 0	d"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'merge-recursive copy vs. rename' '
	git checkout -f copy &&
	git merge rename &&
	( git ls-tree -r HEAD && git ls-files -s ) >actual &&
	(
		echo "100644 blob $o0	b" &&
		echo "100644 blob $o0	c" &&
		echo "100644 blob $o0	d/e" &&
		echo "100644 blob $o0	e" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o0 0	d/e" &&
		echo "100644 $o0 0	e"
	) >expected &&
	test_cmp expected actual
'

test_expect_merge_algorithm failure success 'merge-recursive rename vs. rename/symlink' '

	git checkout -f rename &&
	git merge rename-ln &&
	( git ls-tree -r HEAD && git ls-files -s ) >actual &&
	(
		echo "120000 blob $oln	a" &&
		echo "100644 blob $o0	b" &&
		echo "100644 blob $o0	c" &&
		echo "100644 blob $o0	d/e" &&
		echo "100644 blob $o0	e" &&
		echo "120000 $oln 0	a" &&
		echo "100644 $o0 0	b" &&
		echo "100644 $o0 0	c" &&
		echo "100644 $o0 0	d/e" &&
		echo "100644 $o0 0	e"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'merging with triple rename across D/F conflict' '
	git reset --hard HEAD &&
	git checkout -b topic &&
	git rm -rf . &&

	echo "just a file" >sub1 &&
	mkdir -p sub2 &&
	echo content1 >sub2/file1 &&
	echo content2 >sub2/file2 &&
	echo content3 >sub2/file3 &&
	mkdir simple &&
	echo base >simple/bar &&
	git add -A &&
	test_tick &&
	git commit -m base &&

	git checkout -b other &&
	echo more >>simple/bar &&
	test_tick &&
	git commit -a -m changesimplefile &&

	git checkout topic &&
	git rm sub1 &&
	git mv sub2 sub1 &&
	test_tick &&
	git commit -m changefiletodir &&

	test_tick &&
	git merge other
'

test_expect_success 'merge-recursive remembers the names of all base trees' '
	git reset --hard HEAD &&

	# make the index match $c1 so that merge-recursive below does not
	# fail early
	git diff --binary HEAD $c1 -- | git apply --cached &&

	# more trees than static slots used by oid_to_hex()
	for commit in $c0 $c2 $c4 $c5 $c6 $c7
	do
		git rev-parse "$commit^{tree}" || return 1
	done >trees &&

	# ignore the return code; it only fails because the input is weird...
	test_must_fail git -c merge.verbosity=5 merge-recursive $(cat trees) -- $c1 $c3 >out &&

	# ...but make sure it fails in the expected way
	test_grep CONFLICT.*rename/rename out &&

	# merge-recursive prints in reverse order, but we do not care
	sort <trees >expect &&
	sed -n "s/^virtual //p" out | sort >actual &&
	test_cmp expect actual &&

	git clean -fd
'

test_expect_success 'merge-recursive internal merge resolves to the sameness' '
	git reset --hard HEAD &&

	# We are going to create a history leading to two criss-cross
	# branches A and B.  The common ancestor at the bottom, O0,
	# has two child commits O1 and O2, both of which will be merge
	# base between A and B, like so:
	#
	#       O1---A
	#      /  \ /
	#    O0    .
	#      \  / \
	#       O2---B
	#
	# The recently added "check to see if the index is different from
	# the tree into which something else is getting merged" check must
	# NOT kick in when an inner merge between O1 and O2 is made.  Both
	# O1 and O2 happen to have the same tree as O0 in this test to
	# trigger the bug---whether the inner merge is made by merging O2
	# into O1 or O1 into O2, their common ancestor O0 and the branch
	# being merged have the same tree.  We should not trigger the "is
	# the index dirty?" check in this case.

	echo "zero" >file &&
	git add file &&
	test_tick &&
	git commit -m "O0" &&
	O0=$(git rev-parse HEAD) &&

	test_tick &&
	git commit --allow-empty -m "O1" &&
	O1=$(git rev-parse HEAD) &&

	git reset --hard $O0 &&
	test_tick &&
	git commit --allow-empty -m "O2" &&
	O2=$(git rev-parse HEAD) &&

	test_tick &&
	git merge -s ours $O1 &&
	B=$(git rev-parse HEAD) &&

	git reset --hard $O1 &&
	test_tick &&
	git merge -s ours $O2 &&
	A=$(git rev-parse HEAD) &&

	git merge $B
'

test_done
