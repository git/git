#!/bin/sh

test_description='Test git stash show configuration.'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit file
'

# takes three parameters:
# 1. the stash.showStat value (or "<unset>")
# 2. the stash.showPatch value (or "<unset>")
# 3. the diff options of the expected output (or nothing for no output)
test_stat_and_patch () {
	if test "<unset>" = "$1"
	then
		test_unconfig stash.showStat
	else
		test_config stash.showStat "$1"
	fi &&

	if test "<unset>" = "$2"
	then
		test_unconfig stash.showPatch
	else
		test_config stash.showPatch "$2"
	fi &&

	shift 2 &&
	echo 2 >file.t &&
	if test $# != 0
	then
		git diff "$@" >expect
	fi &&
	git stash &&
	git stash show >actual &&

	if test $# = 0
	then
		test_must_be_empty actual
	else
		test_cmp expect actual
	fi
}

test_expect_success 'showStat unset showPatch unset' '
	test_stat_and_patch "<unset>" "<unset>" --stat
'

test_expect_success 'showStat unset showPatch false' '
	test_stat_and_patch "<unset>" false --stat
'

test_expect_success 'showStat unset showPatch true' '
	test_stat_and_patch "<unset>" true --stat -p
'

test_expect_success 'showStat false showPatch unset' '
	test_stat_and_patch false "<unset>"
'

test_expect_success 'showStat false showPatch false' '
	test_stat_and_patch false false
'

test_expect_success 'showStat false showPatch true' '
	test_stat_and_patch false true -p
'

test_expect_success 'showStat true showPatch unset' '
	test_stat_and_patch true "<unset>" --stat
'

test_expect_success 'showStat true showPatch false' '
	test_stat_and_patch true false --stat
'

test_expect_success 'showStat true showPatch true' '
	test_stat_and_patch true true --stat -p
'

test_done
