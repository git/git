#!/bin/sh

test_description='checkout switching away from an invalid branch'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	echo hello >world &&
	git add world &&
	git commit -m initial
'

test_expect_success 'checkout should not start branch from a tree' '
	test_must_fail git checkout -b newbranch main^{tree}
'

test_expect_success REFFILES 'checkout main from invalid HEAD' '
	echo $ZERO_OID >.git/HEAD &&
	git checkout main --
'

test_expect_success REFFILES 'checkout notices failure to lock HEAD' '
	test_when_finished "rm -f .git/HEAD.lock" &&
	>.git/HEAD.lock &&
	test_must_fail git checkout -b other
'

test_expect_success 'create ref directory/file conflict scenario' '
	git update-ref refs/heads/outer/inner main &&
	reset_to_df () {
		git symbolic-ref HEAD refs/heads/outer
	}
'

test_expect_success 'checkout away from d/f HEAD (unpacked, to branch)' '
	reset_to_df &&
	git checkout main
'

test_expect_success 'checkout away from d/f HEAD (unpacked, to detached)' '
	reset_to_df &&
	git checkout --detach main
'

test_expect_success 'pack refs' '
	git pack-refs --all --prune
'

test_expect_success 'checkout away from d/f HEAD (packed, to branch)' '
	reset_to_df &&
	git checkout main
'

test_expect_success 'checkout away from d/f HEAD (packed, to detached)' '
	reset_to_df &&
	git checkout --detach main
'
test_done
