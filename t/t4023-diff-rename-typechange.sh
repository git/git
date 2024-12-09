#!/bin/sh

test_description='typechange rename detection'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff.sh

test_expect_success setup '

	rm -f foo bar &&
	COPYING_test_data >foo &&
	test_ln_s_add linklink bar &&
	git add foo &&
	git commit -a -m Initial &&
	git tag one &&

	git rm -f foo bar &&
	COPYING_test_data >bar &&
	test_ln_s_add linklink foo &&
	git add bar &&
	git commit -a -m Second &&
	git tag two &&

	git rm -f foo bar &&
	COPYING_test_data >foo &&
	git add foo &&
	git commit -a -m Third &&
	git tag three &&

	mv foo bar &&
	test_ln_s_add linklink foo &&
	git add bar &&
	git commit -a -m Fourth &&
	git tag four &&

	# This is purely for sanity check

	git rm -f foo bar &&
	COPYING_test_data >foo &&
	cat "$TEST_DIRECTORY"/../Makefile >bar &&
	git add foo bar &&
	git commit -a -m Fifth &&
	git tag five &&

	git rm -f foo bar &&
	cat "$TEST_DIRECTORY"/../Makefile >foo &&
	COPYING_test_data >bar &&
	git add foo bar &&
	git commit -a -m Sixth &&
	git tag six

'

test_expect_success 'cross renames to be detected for regular files' '
	git diff-tree five six -r --name-status -B -M >out &&
	sort out >actual &&
	{
		echo "R100	foo	bar" &&
		echo "R100	bar	foo"
	} | sort >expect &&
	test_cmp expect actual

'

test_expect_success 'cross renames to be detected for typechange' '
	git diff-tree one two -r --name-status -B -M >out &&
	sort out >actual &&
	{
		echo "R100	foo	bar" &&
		echo "R100	bar	foo"
	} | sort >expect &&
	test_cmp expect actual

'

test_expect_success 'moves and renames' '
	git diff-tree three four -r --name-status -B -M >out &&
	sort out >actual &&
	{
		# see -B -M (#6) in t4008
		echo "C100	foo	bar" &&
		echo "T100	foo"
	} | sort >expect &&
	test_cmp expect actual

'

test_done
