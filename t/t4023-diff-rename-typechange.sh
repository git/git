#!/bin/sh

test_description='typechange rename detection'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff.sh

test_expect_success setup '

	rm -f foo bar &&
	COPYING_test_data >foo &&
	test_ln_s_add linklink bar &&
	but add foo &&
	but cummit -a -m Initial &&
	but tag one &&

	but rm -f foo bar &&
	COPYING_test_data >bar &&
	test_ln_s_add linklink foo &&
	but add bar &&
	but cummit -a -m Second &&
	but tag two &&

	but rm -f foo bar &&
	COPYING_test_data >foo &&
	but add foo &&
	but cummit -a -m Third &&
	but tag three &&

	mv foo bar &&
	test_ln_s_add linklink foo &&
	but add bar &&
	but cummit -a -m Fourth &&
	but tag four &&

	# This is purely for sanity check

	but rm -f foo bar &&
	COPYING_test_data >foo &&
	cat "$TEST_DIRECTORY"/../Makefile >bar &&
	but add foo bar &&
	but cummit -a -m Fifth &&
	but tag five &&

	but rm -f foo bar &&
	cat "$TEST_DIRECTORY"/../Makefile >foo &&
	COPYING_test_data >bar &&
	but add foo bar &&
	but cummit -a -m Sixth &&
	but tag six

'

test_expect_success 'cross renames to be detected for regular files' '

	but diff-tree five six -r --name-status -B -M | sort >actual &&
	{
		echo "R100	foo	bar" &&
		echo "R100	bar	foo"
	} | sort >expect &&
	test_cmp expect actual

'

test_expect_success 'cross renames to be detected for typechange' '

	but diff-tree one two -r --name-status -B -M | sort >actual &&
	{
		echo "R100	foo	bar" &&
		echo "R100	bar	foo"
	} | sort >expect &&
	test_cmp expect actual

'

test_expect_success 'moves and renames' '

	but diff-tree three four -r --name-status -B -M | sort >actual &&
	{
		# see -B -M (#6) in t4008
		echo "C100	foo	bar" &&
		echo "T100	foo"
	} | sort >expect &&
	test_cmp expect actual

'

test_done
