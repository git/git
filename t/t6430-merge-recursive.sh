#!/bin/sh

test_description='merge-recursive backend test'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-merge.sh

test_expect_success 'setup 1' '

	echo hello >a &&
	o0=$(but hash-object a) &&
	cp a b &&
	cp a c &&
	mkdir d &&
	cp a d/e &&

	test_tick &&
	but add a b c d/e &&
	but cummit -m initial &&
	c0=$(but rev-parse --verify HEAD) &&
	but branch side &&
	but branch df-1 &&
	but branch df-2 &&
	but branch df-3 &&
	but branch remove &&
	but branch submod &&
	but branch copy &&
	but branch rename &&
	but branch rename-ln &&

	echo hello >>a &&
	cp a d/e &&
	o1=$(but hash-object a) &&

	but add a d/e &&

	test_tick &&
	but cummit -m "main modifies a and d/e" &&
	c1=$(but rev-parse --verify HEAD) &&
	( but ls-tree -r HEAD && but ls-files -s ) >actual &&
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
	but checkout side &&
	( but ls-tree -r HEAD && but ls-files -s ) >actual &&
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
	o2=$(but hash-object a) &&

	but add a &&

	test_tick &&
	but cummit -m "side modifies a" &&
	c2=$(but rev-parse --verify HEAD) &&
	( but ls-tree -r HEAD && but ls-files -s ) >actual &&
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
	but checkout df-1 &&
	( but ls-tree -r HEAD && but ls-files -s ) >actual &&
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

	rm -f b && mkdir b && echo df-1 >b/c && but add b/c &&
	o3=$(but hash-object b/c) &&

	test_tick &&
	but cummit -m "df-1 makes b/c" &&
	c3=$(but rev-parse --verify HEAD) &&
	( but ls-tree -r HEAD && but ls-files -s ) >actual &&
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
	but checkout df-2 &&
	( but ls-tree -r HEAD && but ls-files -s ) >actual &&
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

	rm -f a && mkdir a && echo df-2 >a/c && but add a/c &&
	o4=$(but hash-object a/c) &&

	test_tick &&
	but cummit -m "df-2 makes a/c" &&
	c4=$(but rev-parse --verify HEAD) &&
	( but ls-tree -r HEAD && but ls-files -s ) >actual &&
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
	but checkout remove &&
	( but ls-tree -r HEAD && but ls-files -s ) >actual &&
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

	but add a &&
	but rm b &&
	o5=$(but hash-object a) &&

	test_tick &&
	but cummit -m "remove removes b and modifies a" &&
	c5=$(but rev-parse --verify HEAD) &&
	( but ls-tree -r HEAD && but ls-files -s ) >actual &&
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
	but checkout df-3 &&
	( but ls-tree -r HEAD && but ls-files -s ) >actual &&
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

	rm -fr d && echo df-3 >d && but add d &&
	o6=$(but hash-object d) &&

	test_tick &&
	but cummit -m "df-3 makes d" &&
	c6=$(but rev-parse --verify HEAD) &&
	( but ls-tree -r HEAD && but ls-files -s ) >actual &&
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

	but checkout submod &&
	but rm d/e &&
	test_tick &&
	but cummit -m "remove d/e" &&
	but update-index --add --cacheinfo 160000 $c1 d &&
	test_tick &&
	but cummit -m "make d/ a submodule"
'

test_expect_success 'setup 8' '
	but checkout rename &&
	but mv a e &&
	but add e &&
	test_tick &&
	but cummit -m "rename a->e" &&
	c7=$(but rev-parse --verify HEAD) &&
	but checkout rename-ln &&
	but mv a e &&
	test_ln_s_add e a &&
	test_tick &&
	but cummit -m "rename a->e, symlink a->e" &&
	oln=$(printf e | but hash-object --stdin)
'

test_expect_success 'setup 9' '
	but checkout copy &&
	cp a e &&
	but add e &&
	test_tick &&
	but cummit -m "copy a->e"
'

test_expect_success 'merge-recursive simple' '

	rm -fr [abcd] &&
	but checkout -f "$c2" &&

	test_expect_code 1 but merge-recursive "$c0" -- "$c2" "$c1"
'

test_expect_success 'merge-recursive result' '

	but ls-files -s >actual &&
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
	but checkout -f "$c1" &&

	test_must_fail but merge "$c5" &&
	test_must_fail but merge "$c5" 2> out &&
	test_i18ngrep "not possible because you have unmerged files" out &&
	but add -u &&
	test_must_fail but merge "$c5" 2> out &&
	test_i18ngrep "You have not concluded your merge" out &&
	rm -f .but/MERGE_HEAD &&
	test_must_fail but merge "$c5" 2> out &&
	test_i18ngrep "Your local changes to the following files would be overwritten by merge:" out
'

test_expect_success 'merge-recursive remove conflict' '

	rm -fr [abcd] &&
	but checkout -f "$c1" &&

	test_expect_code 1 but merge-recursive "$c0" -- "$c1" "$c5"
'

test_expect_success 'merge-recursive remove conflict' '

	but ls-files -s >actual &&
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
	but reset --hard &&
	but checkout -f "$c1" &&

	but merge-recursive "$c0" -- "$c1" "$c3"
'

test_expect_success 'merge-recursive result' '

	but ls-files -s >actual &&
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
	but reset --hard &&
	but checkout -f "$c1" &&

	test_expect_code 1 but merge-recursive "$c0" -- "$c1" "$c4"
'

test_expect_success 'merge-recursive d/f conflict result' '

	but ls-files -s >actual &&
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
	but reset --hard &&
	but checkout -f "$c4" &&

	test_expect_code 1 but merge-recursive "$c0" -- "$c4" "$c1"
'

test_expect_success 'merge-recursive d/f conflict result the other way' '

	but ls-files -s >actual &&
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
	but reset --hard &&
	but checkout -f "$c1" &&

	test_expect_code 1 but merge-recursive "$c0" -- "$c1" "$c6"
'

test_expect_success 'merge-recursive d/f conflict result' '

	but ls-files -s >actual &&
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
	but reset --hard &&
	but checkout -f "$c6" &&

	test_expect_code 1 but merge-recursive "$c0" -- "$c6" "$c1"
'

test_expect_success 'merge-recursive d/f conflict result' '

	but ls-files -s >actual &&
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
	but init sym &&
	(
		cd sym &&
		ln -s . foo &&
		mkdir bar &&
		>bar/file &&
		but add foo bar/file &&
		but cummit -m "foo symlink" &&

		but checkout -b branch1 &&
		but cummit --allow-empty -m "empty cummit" &&

		but checkout main &&
		but rm foo &&
		mkdir foo &&
		>foo/bar &&
		but add foo/bar &&
		but cummit -m "replace foo symlink with real foo dir and foo/bar file" &&

		but checkout branch1 &&

		but cherry-pick main &&
		test_path_is_dir foo &&
		test_path_is_file foo/bar
	)
'

test_expect_success 'reset and 3-way merge' '

	but reset --hard "$c2" &&
	but read-tree -m "$c0" "$c2" "$c1"

'

test_expect_success 'reset and bind merge' '

	but reset --hard main &&
	but read-tree --prefix=M/ main &&
	but ls-files -s >actual &&
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

	but read-tree --prefix=a1/ main &&
	but ls-files -s >actual &&
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

	but read-tree --prefix=z/ main &&
	but ls-files -s >actual &&
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
		but read-tree -i -m $c7 2>actual-err &&
		test_must_be_empty actual-err &&
		but update-index --ignore-missing --refresh 2>actual-err &&
		test_must_be_empty actual-err &&
		but merge-recursive $c0 -- $c7 $c3 2>actual-err &&
		test_must_be_empty actual-err &&
		but ls-files -s >actual-files 2>actual-err &&
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
		but read-tree -i -m $c3 2>actual-err &&
		test_must_be_empty actual-err &&
		but update-index --ignore-missing --refresh 2>actual-err &&
		test_must_be_empty actual-err &&
		but merge-recursive $c0 -- $c3 $c7 2>actual-err &&
		test_must_be_empty actual-err &&
		but ls-files -s >actual-files 2>actual-err &&
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

	but reset --hard main &&
	but checkout -b rm &&
	but rm d/e &&
	but cummit -mremoved-d/e &&
	but checkout main &&
	but merge -s recursive rm &&
	test_path_is_missing d
'

test_expect_success 'merge-recursive simple w/submodule' '

	but checkout submod &&
	but merge remove
'

test_expect_success 'merge-recursive simple w/submodule result' '

	but ls-files -s >actual &&
	(
		echo "100644 $o5 0	a" &&
		echo "100644 $o0 0	c" &&
		echo "160000 $c1 0	d"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'merge-recursive copy vs. rename' '
	but checkout -f copy &&
	but merge rename &&
	( but ls-tree -r HEAD && but ls-files -s ) >actual &&
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

	but checkout -f rename &&
	but merge rename-ln &&
	( but ls-tree -r HEAD && but ls-files -s ) >actual &&
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
	but reset --hard HEAD &&
	but checkout -b topic &&
	but rm -rf . &&

	echo "just a file" >sub1 &&
	mkdir -p sub2 &&
	echo content1 >sub2/file1 &&
	echo content2 >sub2/file2 &&
	echo content3 >sub2/file3 &&
	mkdir simple &&
	echo base >simple/bar &&
	but add -A &&
	test_tick &&
	but cummit -m base &&

	but checkout -b other &&
	echo more >>simple/bar &&
	test_tick &&
	but cummit -a -m changesimplefile &&

	but checkout topic &&
	but rm sub1 &&
	but mv sub2 sub1 &&
	test_tick &&
	but cummit -m changefiletodir &&

	test_tick &&
	but merge other
'

test_expect_success 'merge-recursive remembers the names of all base trees' '
	but reset --hard HEAD &&

	# make the index match $c1 so that merge-recursive below does not
	# fail early
	but diff --binary HEAD $c1 -- | but apply --cached &&

	# more trees than static slots used by oid_to_hex()
	for cummit in $c0 $c2 $c4 $c5 $c6 $c7
	do
		but rev-parse "$cummit^{tree}" || return 1
	done >trees &&

	# ignore the return code; it only fails because the input is weird...
	test_must_fail but -c merge.verbosity=5 merge-recursive $(cat trees) -- $c1 $c3 >out &&

	# ...but make sure it fails in the expected way
	test_i18ngrep CONFLICT.*rename/rename out &&

	# merge-recursive prints in reverse order, but we do not care
	sort <trees >expect &&
	sed -n "s/^virtual //p" out | sort >actual &&
	test_cmp expect actual &&

	but clean -fd
'

test_expect_success 'merge-recursive internal merge resolves to the sameness' '
	but reset --hard HEAD &&

	# We are going to create a history leading to two criss-cross
	# branches A and B.  The common ancestor at the bottom, O0,
	# has two child cummits O1 and O2, both of which will be merge
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
	but add file &&
	test_tick &&
	but cummit -m "O0" &&
	O0=$(but rev-parse HEAD) &&

	test_tick &&
	but cummit --allow-empty -m "O1" &&
	O1=$(but rev-parse HEAD) &&

	but reset --hard $O0 &&
	test_tick &&
	but cummit --allow-empty -m "O2" &&
	O2=$(but rev-parse HEAD) &&

	test_tick &&
	but merge -s ours $O1 &&
	B=$(but rev-parse HEAD) &&

	but reset --hard $O1 &&
	test_tick &&
	but merge -s ours $O2 &&
	A=$(but rev-parse HEAD) &&

	but merge $B
'

test_done
