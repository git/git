#!/bin/sh

test_description='merge-recursive backend test'

. ./test-lib.sh

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
	git commit -m "master modifies a and d/e" &&
	c1=$(git rev-parse --verify HEAD) &&
	( git ls-tree -r HEAD ; git ls-files -s ) >actual &&
	(
		echo "100644 blob $o1	a"
		echo "100644 blob $o0	b"
		echo "100644 blob $o0	c"
		echo "100644 blob $o1	d/e"
		echo "100644 $o1 0	a"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
		echo "100644 $o1 0	d/e"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'setup 2' '

	rm -rf [abcd] &&
	git checkout side &&
	( git ls-tree -r HEAD ; git ls-files -s ) >actual &&
	(
		echo "100644 blob $o0	a"
		echo "100644 blob $o0	b"
		echo "100644 blob $o0	c"
		echo "100644 blob $o0	d/e"
		echo "100644 $o0 0	a"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
		echo "100644 $o0 0	d/e"
	) >expected &&
	test_cmp expected actual &&

	echo goodbye >>a &&
	o2=$(git hash-object a) &&

	git add a &&

	test_tick &&
	git commit -m "side modifies a" &&
	c2=$(git rev-parse --verify HEAD) &&
	( git ls-tree -r HEAD ; git ls-files -s ) >actual &&
	(
		echo "100644 blob $o2	a"
		echo "100644 blob $o0	b"
		echo "100644 blob $o0	c"
		echo "100644 blob $o0	d/e"
		echo "100644 $o2 0	a"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
		echo "100644 $o0 0	d/e"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'setup 3' '

	rm -rf [abcd] &&
	git checkout df-1 &&
	( git ls-tree -r HEAD ; git ls-files -s ) >actual &&
	(
		echo "100644 blob $o0	a"
		echo "100644 blob $o0	b"
		echo "100644 blob $o0	c"
		echo "100644 blob $o0	d/e"
		echo "100644 $o0 0	a"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
		echo "100644 $o0 0	d/e"
	) >expected &&
	test_cmp expected actual &&

	rm -f b && mkdir b && echo df-1 >b/c && git add b/c &&
	o3=$(git hash-object b/c) &&

	test_tick &&
	git commit -m "df-1 makes b/c" &&
	c3=$(git rev-parse --verify HEAD) &&
	( git ls-tree -r HEAD ; git ls-files -s ) >actual &&
	(
		echo "100644 blob $o0	a"
		echo "100644 blob $o3	b/c"
		echo "100644 blob $o0	c"
		echo "100644 blob $o0	d/e"
		echo "100644 $o0 0	a"
		echo "100644 $o3 0	b/c"
		echo "100644 $o0 0	c"
		echo "100644 $o0 0	d/e"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'setup 4' '

	rm -rf [abcd] &&
	git checkout df-2 &&
	( git ls-tree -r HEAD ; git ls-files -s ) >actual &&
	(
		echo "100644 blob $o0	a"
		echo "100644 blob $o0	b"
		echo "100644 blob $o0	c"
		echo "100644 blob $o0	d/e"
		echo "100644 $o0 0	a"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
		echo "100644 $o0 0	d/e"
	) >expected &&
	test_cmp expected actual &&

	rm -f a && mkdir a && echo df-2 >a/c && git add a/c &&
	o4=$(git hash-object a/c) &&

	test_tick &&
	git commit -m "df-2 makes a/c" &&
	c4=$(git rev-parse --verify HEAD) &&
	( git ls-tree -r HEAD ; git ls-files -s ) >actual &&
	(
		echo "100644 blob $o4	a/c"
		echo "100644 blob $o0	b"
		echo "100644 blob $o0	c"
		echo "100644 blob $o0	d/e"
		echo "100644 $o4 0	a/c"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
		echo "100644 $o0 0	d/e"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'setup 5' '

	rm -rf [abcd] &&
	git checkout remove &&
	( git ls-tree -r HEAD ; git ls-files -s ) >actual &&
	(
		echo "100644 blob $o0	a"
		echo "100644 blob $o0	b"
		echo "100644 blob $o0	c"
		echo "100644 blob $o0	d/e"
		echo "100644 $o0 0	a"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
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
	( git ls-tree -r HEAD ; git ls-files -s ) >actual &&
	(
		echo "100644 blob $o5	a"
		echo "100644 blob $o0	c"
		echo "100644 blob $o0	d/e"
		echo "100644 $o5 0	a"
		echo "100644 $o0 0	c"
		echo "100644 $o0 0	d/e"
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'setup 6' '

	rm -rf [abcd] &&
	git checkout df-3 &&
	( git ls-tree -r HEAD ; git ls-files -s ) >actual &&
	(
		echo "100644 blob $o0	a"
		echo "100644 blob $o0	b"
		echo "100644 blob $o0	c"
		echo "100644 blob $o0	d/e"
		echo "100644 $o0 0	a"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
		echo "100644 $o0 0	d/e"
	) >expected &&
	test_cmp expected actual &&

	rm -fr d && echo df-3 >d && git add d &&
	o6=$(git hash-object d) &&

	test_tick &&
	git commit -m "df-3 makes d" &&
	c6=$(git rev-parse --verify HEAD) &&
	( git ls-tree -r HEAD ; git ls-files -s ) >actual &&
	(
		echo "100644 blob $o0	a"
		echo "100644 blob $o0	b"
		echo "100644 blob $o0	c"
		echo "100644 blob $o6	d"
		echo "100644 $o0 0	a"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
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
	oln=`printf e | git hash-object --stdin`
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
		echo "100644 $o0 1	a"
		echo "100644 $o2 2	a"
		echo "100644 $o1 3	a"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
		echo "100644 $o1 0	d/e"
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'fail if the index has unresolved entries' '

	rm -fr [abcd] &&
	git checkout -f "$c1" &&

	test_must_fail git merge "$c5" &&
	test_must_fail git merge "$c5" 2> out &&
	test_i18ngrep "not possible because you have unmerged files" out &&
	git add -u &&
	test_must_fail git merge "$c5" 2> out &&
	test_i18ngrep "You have not concluded your merge" out &&
	rm -f .git/MERGE_HEAD &&
	test_must_fail git merge "$c5" 2> out &&
	test_i18ngrep "Your local changes to the following files would be overwritten by merge:" out
'

test_expect_success 'merge-recursive remove conflict' '

	rm -fr [abcd] &&
	git checkout -f "$c1" &&

	test_expect_code 1 git merge-recursive "$c0" -- "$c1" "$c5"
'

test_expect_success 'merge-recursive remove conflict' '

	git ls-files -s >actual &&
	(
		echo "100644 $o0 1	a"
		echo "100644 $o1 2	a"
		echo "100644 $o5 3	a"
		echo "100644 $o0 0	c"
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
		echo "100644 $o1 0	a"
		echo "100644 $o3 0	b/c"
		echo "100644 $o0 0	c"
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
		echo "100644 $o0 1	a"
		echo "100644 $o1 2	a"
		echo "100644 $o4 0	a/c"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
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
		echo "100644 $o0 1	a"
		echo "100644 $o1 3	a"
		echo "100644 $o4 0	a/c"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
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
		echo "100644 $o1 0	a"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
		echo "100644 $o6 3	d"
		echo "100644 $o0 1	d/e"
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
		echo "100644 $o1 0	a"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
		echo "100644 $o6 2	d"
		echo "100644 $o0 1	d/e"
		echo "100644 $o1 3	d/e"
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'reset and 3-way merge' '

	git reset --hard "$c2" &&
	git read-tree -m "$c0" "$c2" "$c1"

'

test_expect_success 'reset and bind merge' '

	git reset --hard master &&
	git read-tree --prefix=M/ master &&
	git ls-files -s >actual &&
	(
		echo "100644 $o1 0	M/a"
		echo "100644 $o0 0	M/b"
		echo "100644 $o0 0	M/c"
		echo "100644 $o1 0	M/d/e"
		echo "100644 $o1 0	a"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
		echo "100644 $o1 0	d/e"
	) >expected &&
	test_cmp expected actual &&

	git read-tree --prefix=a1/ master &&
	git ls-files -s >actual &&
	(
		echo "100644 $o1 0	M/a"
		echo "100644 $o0 0	M/b"
		echo "100644 $o0 0	M/c"
		echo "100644 $o1 0	M/d/e"
		echo "100644 $o1 0	a"
		echo "100644 $o1 0	a1/a"
		echo "100644 $o0 0	a1/b"
		echo "100644 $o0 0	a1/c"
		echo "100644 $o1 0	a1/d/e"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
		echo "100644 $o1 0	d/e"
	) >expected &&
	test_cmp expected actual &&

	git read-tree --prefix=z/ master &&
	git ls-files -s >actual &&
	(
		echo "100644 $o1 0	M/a"
		echo "100644 $o0 0	M/b"
		echo "100644 $o0 0	M/c"
		echo "100644 $o1 0	M/d/e"
		echo "100644 $o1 0	a"
		echo "100644 $o1 0	a1/a"
		echo "100644 $o0 0	a1/b"
		echo "100644 $o0 0	a1/c"
		echo "100644 $o1 0	a1/d/e"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
		echo "100644 $o1 0	d/e"
		echo "100644 $o1 0	z/a"
		echo "100644 $o0 0	z/b"
		echo "100644 $o0 0	z/c"
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
		git read-tree -i -m $c7 &&
		git update-index --ignore-missing --refresh &&
		git merge-recursive $c0 -- $c7 $c3 &&
		git ls-files -s >actual-files
	) 2>actual-err &&
	>expected-err &&
	cat >expected-files <<-EOF &&
	100644 $o3 0	b/c
	100644 $o0 0	c
	100644 $o0 0	d/e
	100644 $o0 0	e
	EOF
	test_cmp expected-files actual-files &&
	test_cmp expected-err actual-err
'

test_expect_success 'merge-recursive w/ empty work tree - theirs has rename' '
	(
		GIT_WORK_TREE="$PWD/theirs-has-rename-work" &&
		export GIT_WORK_TREE &&
		GIT_INDEX_FILE="$PWD/theirs-has-rename-index" &&
		export GIT_INDEX_FILE &&
		mkdir "$GIT_WORK_TREE" &&
		git read-tree -i -m $c3 &&
		git update-index --ignore-missing --refresh &&
		git merge-recursive $c0 -- $c3 $c7 &&
		git ls-files -s >actual-files
	) 2>actual-err &&
	>expected-err &&
	cat >expected-files <<-EOF &&
	100644 $o3 0	b/c
	100644 $o0 0	c
	100644 $o0 0	d/e
	100644 $o0 0	e
	EOF
	test_cmp expected-files actual-files &&
	test_cmp expected-err actual-err
'

test_expect_success 'merge removes empty directories' '

	git reset --hard master &&
	git checkout -b rm &&
	git rm d/e &&
	git commit -mremoved-d/e &&
	git checkout master &&
	git merge -s recursive rm &&
	test_must_fail test -d d
'

test_expect_failure 'merge-recursive simple w/submodule' '

	git checkout submod &&
	git merge remove
'

test_expect_failure 'merge-recursive simple w/submodule result' '

	git ls-files -s >actual &&
	(
		echo "100644 $o5 0	a"
		echo "100644 $o0 0	c"
		echo "160000 $c1 0	d"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'merge-recursive copy vs. rename' '
	git checkout -f copy &&
	git merge rename &&
	( git ls-tree -r HEAD && git ls-files -s ) >actual &&
	(
		echo "100644 blob $o0	b"
		echo "100644 blob $o0	c"
		echo "100644 blob $o0	d/e"
		echo "100644 blob $o0	e"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
		echo "100644 $o0 0	d/e"
		echo "100644 $o0 0	e"
	) >expected &&
	test_cmp expected actual
'

test_expect_failure 'merge-recursive rename vs. rename/symlink' '

	git checkout -f rename &&
	git merge rename-ln &&
	( git ls-tree -r HEAD ; git ls-files -s ) >actual &&
	(
		echo "120000 blob $oln	a"
		echo "100644 blob $o0	b"
		echo "100644 blob $o0	c"
		echo "100644 blob $o0	d/e"
		echo "100644 blob $o0	e"
		echo "120000 $oln 0	a"
		echo "100644 $o0 0	b"
		echo "100644 $o0 0	c"
		echo "100644 $o0 0	d/e"
		echo "100644 $o0 0	e"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'merging with triple rename across D/F conflict' '
	git reset --hard HEAD &&
	git checkout -b main &&
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

	git checkout main &&
	git rm sub1 &&
	git mv sub2 sub1 &&
	test_tick &&
	git commit -m changefiletodir &&

	test_tick &&
	git merge other
'

test_done
