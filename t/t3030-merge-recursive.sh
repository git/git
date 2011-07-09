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
	if test_have_prereq SYMLINKS
	then
		git branch rename-ln
	fi &&

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
	if test_have_prereq SYMLINKS
	then
		git checkout rename-ln &&
		git mv a e &&
		ln -s e a &&
		git add a e &&
		test_tick &&
		git commit -m "rename a->e, symlink a->e"
	fi
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

	git merge-recursive "$c0" -- "$c2" "$c1"
	status=$?
	case "$status" in
	1)
		: happy
		;;
	*)
		echo >&2 "why status $status!!!"
		false
		;;
	esac
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

	git merge-recursive "$c0" -- "$c1" "$c5"
	status=$?
	case "$status" in
	1)
		: happy
		;;
	*)
		echo >&2 "why status $status!!!"
		false
		;;
	esac
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

	git merge-recursive "$c0" -- "$c1" "$c4"
	status=$?
	case "$status" in
	1)
		: happy
		;;
	*)
		echo >&2 "why status $status!!!"
		false
		;;
	esac
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

	git merge-recursive "$c0" -- "$c4" "$c1"
	status=$?
	case "$status" in
	1)
		: happy
		;;
	*)
		echo >&2 "why status $status!!!"
		false
		;;
	esac
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

	git merge-recursive "$c0" -- "$c1" "$c6"
	status=$?
	case "$status" in
	1)
		: happy
		;;
	*)
		echo >&2 "why status $status!!!"
		false
		;;
	esac
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

	git merge-recursive "$c0" -- "$c6" "$c1"
	status=$?
	case "$status" in
	1)
		: happy
		;;
	*)
		echo >&2 "why status $status!!!"
		false
		;;
	esac
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

if test_have_prereq SYMLINKS
then
	test_expect_success 'merge-recursive rename vs. rename/symlink' '

		git checkout -f rename &&
		git merge rename-ln &&
		( git ls-tree -r HEAD ; git ls-files -s ) >actual &&
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
fi


test_done
