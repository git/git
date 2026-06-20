#!/bin/sh

test_description='git refs delete'

. ./test-lib.sh

setup_repo () {
	git init "$1" &&
	test_commit -C "$1" A &&
	test_commit -C "$1" B
}

test_expect_success 'delete without oldvalue verification' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	A=$(git -C repo rev-parse A) &&
	git -C repo update-ref refs/heads/foo $A &&
	git -C repo refs delete refs/heads/foo &&
	test_must_fail git -C repo show-ref --verify -q refs/heads/foo
'

test_expect_success 'delete with matching oldvalue' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git update-ref refs/heads/foo $A &&
		git refs delete refs/heads/foo $A &&
		test_must_fail git refs exists refs/heads/foo
	)
'

test_expect_success 'delete with stale oldvalue fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		B=$(git rev-parse B) &&
		git update-ref refs/heads/foo $A &&
		test_must_fail git refs delete refs/heads/foo $B 2>err &&
		test_grep " but expected " err &&
		git refs exists refs/heads/foo
	)
'

test_expect_success 'delete with null oldvalue fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git update-ref refs/heads/foo $A &&
		test_must_fail git refs delete refs/heads/foo $ZERO_OID 2>err &&
		test_grep "null old object ID" err &&
		git refs exists refs/heads/foo
	)
'

test_expect_success 'delete with invalid oldvalue fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git update-ref refs/heads/foo $A &&
		test_must_fail git refs delete refs/heads/foo invalid-oid 2>err &&
		test_grep "invalid old object ID" err &&
		git refs exists refs/heads/foo
	)
'

test_expect_success 'delete symref with --no-deref leaves target intact' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git update-ref refs/heads/foo $A &&
		git symbolic-ref refs/heads/symref refs/heads/foo &&
		git refs delete --no-deref refs/heads/symref &&
		test_must_fail git refs exists refs/heads/symref &&
		git refs exists refs/heads/foo
	)
'

test_expect_success 'delete with message records reason in reflog' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git update-ref refs/heads/foo $A &&
		git symbolic-ref HEAD refs/heads/foo &&
		git refs delete --message=delete-reason refs/heads/foo &&
		test_must_fail git refs exists refs/heads/foo &&
		test-tool ref-store main for-each-reflog-ent HEAD >actual &&
		test_grep "delete-reason$" actual
	)
'

test_expect_success 'delete with empty message fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git update-ref refs/heads/foo $A &&
		test_must_fail git refs delete --message= refs/heads/foo 2>err &&
		test_grep "empty message" err &&
		git refs exists refs/heads/foo
	)
'

test_expect_success 'delete without arguments fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	test_must_fail git -C repo refs delete 2>err &&
	test_grep "requires reference name" err
'

test_expect_success 'delete with too many arguments fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	test_must_fail git refs delete one two three 2>err &&
	test_grep "requires reference name" err
'

test_done
