#!/bin/sh

test_description='checkout switching away from an invalid branch'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	echo hello >world &&
	but add world &&
	but cummit -m initial
'

test_expect_success 'checkout should not start branch from a tree' '
	test_must_fail but checkout -b newbranch main^{tree}
'

test_expect_success 'checkout main from invalid HEAD' '
	echo $ZERO_OID >.but/HEAD &&
	but checkout main --
'

test_expect_success 'checkout notices failure to lock HEAD' '
	test_when_finished "rm -f .but/HEAD.lock" &&
	>.but/HEAD.lock &&
	test_must_fail but checkout -b other
'

test_expect_success 'create ref directory/file conflict scenario' '
	but update-ref refs/heads/outer/inner main &&

	# do not rely on symbolic-ref to get a known state,
	# as it may use the same code we are testing
	reset_to_df () {
		echo "ref: refs/heads/outer" >.but/HEAD
	}
'

test_expect_success 'checkout away from d/f HEAD (unpacked, to branch)' '
	reset_to_df &&
	but checkout main
'

test_expect_success 'checkout away from d/f HEAD (unpacked, to detached)' '
	reset_to_df &&
	but checkout --detach main
'

test_expect_success 'pack refs' '
	but pack-refs --all --prune
'

test_expect_success 'checkout away from d/f HEAD (packed, to branch)' '
	reset_to_df &&
	but checkout main
'

test_expect_success 'checkout away from d/f HEAD (packed, to detached)' '
	reset_to_df &&
	but checkout --detach main
'
test_done
