#!/bin/sh

test_description='git refs rename'

. ./test-lib.sh

setup_repo () {
	git init "$1" &&
	test_commit -C "$1" A &&
	test_commit -C "$1" B
}

test_ref_matches () {
	git rev-parse "$1" >expect &&
	echo "$2" >actual &&
	test_cmp expect actual
}

test_expect_success 'rename an existing reference' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git refs update refs/heads/foo $A &&
		git refs rename refs/heads/foo refs/heads/bar &&
		test_must_fail git refs exists refs/heads/foo &&
		test_ref_matches refs/heads/bar $A
	)
'

test_expect_success 'rename moves the reflog along with the reference' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git refs update --message="rename me" refs/heads/foo $A &&
		git refs rename refs/heads/foo refs/heads/bar &&
		git reflog show refs/heads/bar >reflog &&
		test_grep "rename me" reflog &&
		test_must_fail git reflog exists refs/heads/foo
	)
'

test_expect_success 'rename with message records reason in reflog' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git refs update refs/heads/foo $A &&
		git refs rename --message="rename reason" refs/heads/foo refs/heads/bar &&
		git reflog show refs/heads/bar >actual &&
		test_grep "rename reason" actual
	)
'

test_expect_success 'rename a nonexistent reference fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		test_must_fail git refs rename refs/heads/foo refs/heads/bar 2>err &&
		test_grep "reference does not exist" err
	)
'

test_expect_success 'rename to an existing reference fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		B=$(git rev-parse B) &&
		git refs update refs/heads/foo $A &&
		git refs update refs/heads/bar $B &&
		test_must_fail git refs rename refs/heads/foo refs/heads/bar 2>err &&
		test_grep "reference already exists" err
	)
'

test_expect_success 'rename with empty message fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git refs update refs/heads/foo $A &&
		test_must_fail git refs rename --message= refs/heads/foo refs/heads/bar 2>err &&
		test_grep "empty message" err
	)
'

test_expect_success 'rename with invalid old reference name fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		test_must_fail git refs rename "refs/heads/foo..bar" refs/heads/bar 2>err &&
		test_grep "invalid ref format" err
	)
'

test_expect_success 'rename with invalid new reference name fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git refs update refs/heads/foo $A &&
		test_must_fail git refs rename refs/heads/foo "refs/heads/bar..baz" 2>err &&
		test_grep "invalid ref format" err
	)
'

test_expect_success 'rename with too few arguments fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	test_must_fail git -C repo refs rename refs/heads/foo 2>err &&
	test_grep "requires old and new reference name" err
'

test_expect_success 'rename with too many arguments fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	test_must_fail git -C repo refs rename refs/heads/foo refs/heads/bar refs/heads/baz 2>err &&
	test_grep "requires old and new reference name" err
'

test_done
