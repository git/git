#!/bin/sh

test_description='git refs update'

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

test_expect_success 'update creates a new reference' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git refs update refs/heads/foo $A &&
		test_ref_matches refs/heads/foo "$A"
	)
'

test_expect_success 'update an existing reference without oldvalue' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		B=$(git rev-parse B) &&
		git refs update refs/heads/foo $A &&
		git refs update refs/heads/foo $B &&
		test_ref_matches refs/heads/foo $B
	)
'

test_expect_success 'update with matching oldvalue' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		B=$(git rev-parse B) &&
		git refs update refs/heads/foo $A &&
		git refs update refs/heads/foo $B $A &&
		test_ref_matches refs/heads/foo $B
	)
'

test_expect_success 'update with stale oldvalue fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		B=$(git rev-parse B) &&
		git refs update refs/heads/foo $A &&
		test_must_fail git refs update refs/heads/foo $B $B 2>err &&
		test_grep " but expected " err &&
		test_ref_matches refs/heads/foo $A
	)
'

test_expect_success 'update can create a new branch with oldvalue' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git refs update refs/heads/foo $A $ZERO_OID 2>err &&
		test_ref_matches refs/heads/foo $A
	)
'

test_expect_success 'update can create a new branch without oldvalue' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git refs update refs/heads/foo $A 2>err &&
		test_ref_matches refs/heads/foo $A
	)
'

test_expect_success 'update refuses to create preexisting branch' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		B=$(git rev-parse B) &&
		git refs update refs/heads/foo $A &&
		test_must_fail git refs update refs/heads/foo $B $ZERO_OID 2>err &&
		test_grep "reference already exists" err &&
		test_ref_matches refs/heads/foo $A
	)
'

test_expect_success 'update can delete a branch with oldvalue' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git refs update refs/heads/foo $A 2>err &&
		git refs update refs/heads/foo $ZERO_OID $A 2>err &&
		test_must_fail git refs exists refs/heads/foo
	)
'

test_expect_success 'update can delete a branch without oldvalue' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git refs update refs/heads/foo $A 2>err &&
		git refs update refs/heads/foo $ZERO_OID 2>err &&
		test_must_fail git refs exists refs/heads/foo
	)
'

test_expect_success 'update refuses to delete a branch with mismatching value' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		B=$(git rev-parse B) &&
		git refs update refs/heads/foo $A 2>err &&
		test_must_fail git refs update refs/heads/foo $ZERO_OID $B 2>err &&
		test_grep " but expected " err &&
		git refs exists refs/heads/foo
	)
'

test_expect_success 'update refuses to create preexisting branch' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		B=$(git rev-parse B) &&
		git refs update refs/heads/foo $A &&
		test_must_fail git refs update refs/heads/foo $B $ZERO_OID 2>err &&
		test_grep "reference already exists" err &&
		test_ref_matches refs/heads/foo $A
	)
'


test_expect_success 'update with invalid new value fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		test_must_fail git refs update refs/heads/foo invalid-oid 2>err &&
		test_grep "invalid new object ID" err &&
		test_must_fail git refs exists refs/heads/foo
	)
'

test_expect_success 'update with invalid old value fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		B=$(git rev-parse B) &&
		git refs update refs/heads/foo $A &&
		test_must_fail git refs update refs/heads/foo $B invalid-oid 2>err &&
		test_grep "invalid old object ID" err &&
		test_ref_matches refs/heads/foo $A
	)
'

test_expect_success 'update --no-deref rewrites the symref itself' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		B=$(git rev-parse B) &&
		git refs update refs/heads/foo $A &&
		git symbolic-ref refs/heads/symref refs/heads/foo &&
		git refs update --no-deref refs/heads/symref $B &&
		test_must_fail git symbolic-ref refs/heads/symref &&
		test_ref_matches refs/heads/symref $B &&
		test_ref_matches refs/heads/foo $A
	)
'

test_expect_success 'update does not create a reflog by default' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git refs update refs/foo $A &&
		test_must_fail git reflog exists refs/foo
	)
'

test_expect_success 'update creates a reflog with --create-reflog' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git refs update --create-reflog refs/foo $A &&
		git reflog exists refs/foo
	)
'

test_expect_success 'update with message records reason in reflog' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		B=$(git rev-parse B) &&
		git refs update refs/heads/foo $A &&
		git refs update --message=update-reason refs/heads/foo $B &&
		git reflog show refs/heads/foo >actual &&
		test_grep "update-reason$" actual
	)
'

test_expect_success 'update with empty message fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		B=$(git rev-parse B) &&
		git refs update refs/heads/foo $A &&
		test_must_fail git refs update --message= refs/heads/foo $B 2>err &&
		test_grep "empty message" err
	)
'

test_expect_success 'update with too few arguments fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	test_must_fail git -C repo refs update refs/heads/foo 2>err &&
	test_grep "requires reference name, new value" err
'

test_expect_success 'update with too many arguments fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		B=$(git rev-parse B) &&
		test_must_fail git refs update refs/heads/foo $A $B extra 2>err &&
		test_grep "requires reference name, new value" err
	)
'

test_done
