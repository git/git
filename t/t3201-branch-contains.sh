#!/bin/sh

test_description='branch --contains <cummit>, --no-contains <cummit> --merged, and --no-merged'

. ./test-lib.sh

test_expect_success setup '

	>file &&
	but add file &&
	test_tick &&
	but cummit -m initial &&
	but branch -M main &&
	but branch side &&

	echo 1 >file &&
	test_tick &&
	but cummit -a -m "second on main" &&

	but checkout side &&
	echo 1 >file &&
	test_tick &&
	but cummit -a -m "second on side" &&

	but merge main

'

test_expect_success 'branch --contains=main' '

	but branch --contains=main >actual &&
	{
		echo "  main" && echo "* side"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --contains main' '

	but branch --contains main >actual &&
	{
		echo "  main" && echo "* side"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --no-contains=main' '

	but branch --no-contains=main >actual &&
	test_must_be_empty actual

'

test_expect_success 'branch --no-contains main' '

	but branch --no-contains main >actual &&
	test_must_be_empty actual

'

test_expect_success 'branch --contains=side' '

	but branch --contains=side >actual &&
	{
		echo "* side"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --no-contains=side' '

	but branch --no-contains=side >actual &&
	{
		echo "  main"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --contains with pattern implies --list' '

	but branch --contains=main main >actual &&
	{
		echo "  main"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --no-contains with pattern implies --list' '

	but branch --no-contains=main main >actual &&
	test_must_be_empty actual

'

test_expect_success 'side: branch --merged' '

	but branch --merged >actual &&
	{
		echo "  main" &&
		echo "* side"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --merged with pattern implies --list' '

	but branch --merged=side main >actual &&
	{
		echo "  main"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'side: branch --no-merged' '

	but branch --no-merged >actual &&
	test_must_be_empty actual

'

test_expect_success 'main: branch --merged' '

	but checkout main &&
	but branch --merged >actual &&
	{
		echo "* main"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'main: branch --no-merged' '

	but branch --no-merged >actual &&
	{
		echo "  side"
	} >expect &&
	test_cmp expect actual

'

test_expect_success 'branch --no-merged with pattern implies --list' '

	but branch --no-merged=main main >actual &&
	test_must_be_empty actual

'

test_expect_success 'implicit --list conflicts with modification options' '

	test_must_fail but branch --contains=main -d &&
	test_must_fail but branch --contains=main -m foo &&
	test_must_fail but branch --no-contains=main -d &&
	test_must_fail but branch --no-contains=main -m foo

'

test_expect_success 'Assert that --contains only works on cummits, not trees & blobs' '
	test_must_fail but branch --contains main^{tree} &&
	blob=$(but hash-object -w --stdin <<-\EOF
	Some blob
	EOF
	) &&
	test_must_fail but branch --contains $blob &&
	test_must_fail but branch --no-contains $blob
'

test_expect_success 'multiple branch --contains' '
	but checkout -b side2 main &&
	>feature &&
	but add feature &&
	but cummit -m "add feature" &&
	but checkout -b next main &&
	but merge side &&
	but branch --contains side --contains side2 >actual &&
	cat >expect <<-\EOF &&
	* next
	  side
	  side2
	EOF
	test_cmp expect actual
'

test_expect_success 'multiple branch --merged' '
	but branch --merged next --merged main >actual &&
	cat >expect <<-\EOF &&
	  main
	* next
	  side
	EOF
	test_cmp expect actual
'

test_expect_success 'multiple branch --no-contains' '
	but branch --no-contains side --no-contains side2 >actual &&
	cat >expect <<-\EOF &&
	  main
	EOF
	test_cmp expect actual
'

test_expect_success 'multiple branch --no-merged' '
	but branch --no-merged next --no-merged main >actual &&
	cat >expect <<-\EOF &&
	  side2
	EOF
	test_cmp expect actual
'

test_expect_success 'branch --contains combined with --no-contains' '
	but checkout -b seen main &&
	but merge side &&
	but merge side2 &&
	but branch --contains side --no-contains side2 >actual &&
	cat >expect <<-\EOF &&
	  next
	  side
	EOF
	test_cmp expect actual
'

test_expect_success 'branch --merged combined with --no-merged' '
	but branch --merged seen --no-merged next >actual &&
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
# Here "topic" tracks "main" with one extra cummit, and "zzz" points to the
# same tip as main The name "zzz" must come alphabetically after "topic"
# as we process them in that order.
test_expect_success 'branch --merged with --verbose' '
	but branch --track topic main &&
	but branch zzz topic &&
	but checkout topic &&
	test_cummit foo &&
	but branch --merged topic >actual &&
	cat >expect <<-\EOF &&
	  main
	* topic
	  zzz
	EOF
	test_cmp expect actual &&
	but branch --verbose --merged topic >actual &&
	cat >expect <<-EOF &&
	  main  $(but rev-parse --short main) second on main
	* topic $(but rev-parse --short topic ) [ahead 1] foo
	  zzz   $(but rev-parse --short zzz   ) second on main
	EOF
	test_cmp expect actual
'

test_done
