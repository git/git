#!/bin/sh

test_description='git apply --3way'

. ./test-lib.sh

create_file () {
	for i
	do
		echo "$i"
	done
}

sanitize_conflicted_diff () {
	sed -e '
		/^index /d
		s/^\(+[<>][<>][<>][<>]*\) .*/\1/
	'
}

test_expect_success setup '
	test_tick &&
	create_file >one 1 2 3 4 5 6 7 &&
	cat one >two &&
	git add one two &&
	git commit -m initial &&

	git branch side &&

	test_tick &&
	create_file >one 1 two 3 4 5 six 7 &&
	create_file >two 1 two 3 4 5 6 7 &&
	git commit -a -m master &&

	git checkout side &&
	create_file >one 1 2 3 4 five 6 7 &&
	create_file >two 1 2 3 4 five 6 7 &&
	git commit -a -m side &&

	git checkout master
'

test_expect_success 'apply without --3way' '
	git diff side^ side >P.diff &&

	# should fail to apply
	git reset --hard &&
	git checkout master^0 &&
	test_must_fail git apply --index P.diff &&
	# should leave things intact
	git diff-files --exit-code &&
	git diff-index --exit-code --cached HEAD
'

test_expect_success 'apply with --3way' '
	# Merging side should be similar to applying this patch
	git diff ...side >P.diff &&

	# The corresponding conflicted merge
	git reset --hard &&
	git checkout master^0 &&
	test_must_fail git merge --no-commit side &&
	git ls-files -s >expect.ls &&
	git diff HEAD | sanitize_conflicted_diff >expect.diff &&

	# should fail to apply
	git reset --hard &&
	git checkout master^0 &&
	test_must_fail git apply --index --3way P.diff &&
	git ls-files -s >actual.ls &&
	git diff HEAD | sanitize_conflicted_diff >actual.diff &&

	# The result should resemble the corresponding merge
	test_cmp expect.ls actual.ls &&
	test_cmp expect.diff actual.diff
'

test_done
