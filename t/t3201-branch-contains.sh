#!/bin/sh

test_description='branch --contains <commit>, --no-contains <commit> --merged, and --no-merged'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '

	>file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	git branch -M main &&
	git branch side &&

	echo 1 >file &&
	test_tick &&
	git commit -a -m "second on main" &&

	git checkout side &&
	echo 1 >file &&
	test_tick &&
	git commit -a -m "second on side" &&

	git merge main

'

test_expect_success 'branch --contains=main' '

	git branch --contains=main >actual &&
	{
		echo "  main" && echo "* side"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --contains main' '

	git branch --contains main >actual &&
	{
		echo "  main" && echo "* side"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --no-contains=main' '

	git branch --no-contains=main >actual &&
	test_must_be_empty actual

'

test_expect_success 'branch --no-contains main' '

	git branch --no-contains main >actual &&
	test_must_be_empty actual

'

test_expect_success 'branch --contains=side' '

	git branch --contains=side >actual &&
	{
		echo "* side"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --no-contains=side' '

	git branch --no-contains=side >actual &&
	{
		echo "  main"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --contains with pattern implies --list' '

	git branch --contains=main main >actual &&
	{
		echo "  main"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --no-contains with pattern implies --list' '

	git branch --no-contains=main main >actual &&
	test_must_be_empty actual

'

test_expect_success 'side: branch --merged' '

	git branch --merged >actual &&
	{
		echo "  main" &&
		echo "* side"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --merged with pattern implies --list' '

	git branch --merged=side main >actual &&
	{
		echo "  main"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'side: branch --no-merged' '

	git branch --no-merged >actual &&
	test_must_be_empty actual

'

test_expect_success 'main: branch --merged' '

	git checkout main &&
	git branch --merged >actual &&
	{
		echo "* main"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'main: branch --no-merged' '

	git branch --no-merged >actual &&
	{
		echo "  side"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --no-merged with pattern implies --list' '

	git branch --no-merged=main main >actual &&
	test_must_be_empty actual

'

test_expect_success 'implicit --list conflicts with modification options' '

	test_must_fail git branch --contains=main -d &&
	test_must_fail git branch --contains=main -m foo &&
	test_must_fail git branch --no-contains=main -d &&
	test_must_fail git branch --no-contains=main -m foo

'

test_expect_success 'Assert that --contains only works on commits, not trees & blobs' '
	test_must_fail git branch --contains main^{tree} &&
	blob=$(git hash-object -w --stdin <<-\EOF
	Some blob
	EOF
	) &&
	test_must_fail git branch --contains $blob &&
	test_must_fail git branch --no-contains $blob
'

test_expect_success 'multiple branch --contains' '
	git checkout -b side2 main &&
	>feature &&
	git add feature &&
	git commit -m "add feature" &&
	git checkout -b next main &&
	git merge side &&
	git branch --contains side --contains side2 >actual &&
	cat >expect <<-\EOF &&
	* next
	  side
	  side2
	EOF
	test_cmp expect actual
'

test_expect_success 'multiple branch --merged' '
	git branch --merged next --merged main >actual &&
	cat >expect <<-\EOF &&
	  main
	* next
	  side
	EOF
	test_cmp expect actual
'

test_expect_success 'multiple branch --no-contains' '
	git branch --no-contains side --no-contains side2 >actual &&
	cat >expect <<-\EOF &&
	  main
	EOF
	test_cmp expect actual
'

test_expect_success 'multiple branch --no-merged' '
	git branch --no-merged next --no-merged main >actual &&
	cat >expect <<-\EOF &&
	  side2
	EOF
	test_cmp expect actual
'

test_expect_success 'branch --contains combined with --no-contains' '
	git checkout -b seen main &&
	git merge side &&
	git merge side2 &&
	git branch --contains side --no-contains side2 >actual &&
	cat >expect <<-\EOF &&
	  next
	  side
	EOF
	test_cmp expect actual
'

test_expect_success 'branch --merged combined with --no-merged' '
	git branch --merged seen --no-merged next >actual &&
	cat >expect <<-\EOF &&
	* seen
	  side2
	EOF
	test_cmp expect actual
'

# We want to set up a case where the walk for the tracking info
# of one branch crosses the tip of another branch (and make sure
# that the latter walk does not mess up our flag to see if it was
# merged).
#
# Here "topic" tracks "main" with one extra commit, and "zzz" points to the
# same tip as main The name "zzz" must come alphabetically after "topic"
# as we process them in that order.
test_expect_success 'branch --merged with --verbose' '
	git branch --track topic main &&
	git branch zzz topic &&
	git checkout topic &&
	test_commit foo &&
	git branch --merged topic >actual &&
	cat >expect <<-\EOF &&
	  main
	* topic
	  zzz
	EOF
	test_cmp expect actual &&
	git branch --verbose --merged topic >actual &&
	cat >expect <<-EOF &&
	  main  $(git rev-parse --short main) second on main
	* topic $(git rev-parse --short topic ) [ahead 1] foo
	  zzz   $(git rev-parse --short zzz   ) second on main
	EOF
	test_cmp expect actual
'

test_done
