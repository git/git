#!/bin/sh

test_description='branch --contains <commit>, --merged, and --no-merged'

. ./test-lib.sh

test_expect_success setup '

	>file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	git branch side &&

	echo 1 >file &&
	test_tick &&
	git commit -a -m "second on master" &&

	git checkout side &&
	echo 1 >file &&
	test_tick &&
	git commit -a -m "second on side" &&

	git merge master

'

test_expect_success 'branch --contains=master' '

	git branch --contains=master >actual &&
	{
		echo "  master" && echo "* side"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --contains master' '

	git branch --contains master >actual &&
	{
		echo "  master" && echo "* side"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --contains=side' '

	git branch --contains=side >actual &&
	{
		echo "* side"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --contains with pattern implies --list' '

	git branch --contains=master master >actual &&
	{
		echo "  master"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'side: branch --merged' '

	git branch --merged >actual &&
	{
		echo "  master" &&
		echo "* side"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --merged with pattern implies --list' '

	git branch --merged=side master >actual &&
	{
		echo "  master"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'side: branch --no-merged' '

	git branch --no-merged >actual &&
	>expect &&
	test_cmp expect actual

'

test_expect_success 'master: branch --merged' '

	git checkout master &&
	git branch --merged >actual &&
	{
		echo "* master"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'master: branch --no-merged' '

	git branch --no-merged >actual &&
	{
		echo "  side"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --no-merged with pattern implies --list' '

	git branch --no-merged=master master >actual &&
	>expect &&
	test_cmp expect actual

'

test_expect_success 'implicit --list conflicts with modification options' '

	test_must_fail git branch --contains=master -d &&
	test_must_fail git branch --contains=master -m foo

'

# We want to set up a case where the walk for the tracking info
# of one branch crosses the tip of another branch (and make sure
# that the latter walk does not mess up our flag to see if it was
# merged).
#
# Here "topic" tracks "master" with one extra commit, and "zzz" points to the
# same tip as master The name "zzz" must come alphabetically after "topic"
# as we process them in that order.
test_expect_success 'branch --merged with --verbose' '
	git branch --track topic master &&
	git branch zzz topic &&
	git checkout topic &&
	test_commit foo &&
	git branch --merged topic >actual &&
	cat >expect <<-\EOF &&
	  master
	* topic
	  zzz
	EOF
	test_cmp expect actual &&
	git branch --verbose --merged topic >actual &&
	cat >expect <<-\EOF &&
	  master c77a0a9 second on master
	* topic  2c939f4 [ahead 1] foo
	  zzz    c77a0a9 second on master
	EOF
	test_i18ncmp expect actual
'

test_done
