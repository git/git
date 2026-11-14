#!/bin/sh

test_description='git refs create'

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

test_expect_success 'create a new reference' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git refs create refs/heads/foo $A &&
		test_ref_matches refs/heads/foo "$A"
	)
'

test_expect_success 'create fails when the reference already exists' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		B=$(git rev-parse B) &&
		git refs create refs/heads/foo $A &&
		test_must_fail git refs create refs/heads/foo $B 2>err &&
		test_grep "reference already exists" err &&
		test_ref_matches refs/heads/foo "$A"
	)
'

test_expect_success 'create with null new value fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		test_must_fail git refs create refs/heads/foo $ZERO_OID 2>err &&
		test_grep "null new object ID" err &&
		test_must_fail git refs exists refs/heads/foo
	)
'

test_expect_success 'create with invalid new value fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		test_must_fail git refs create refs/heads/foo invalid-oid 2>err &&
		test_grep "invalid object ID" err &&
		test_must_fail git refs exists refs/heads/foo
	)
'

test_expect_success 'create does not create a reflog by default' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git refs create refs/foo $A &&
		test_must_fail git reflog exists refs/foo
	)
'

test_expect_success 'create creates a reflog with --create-reflog' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git refs create --create-reflog refs/foo $A &&
		git reflog exists refs/foo
	)
'

test_expect_success 'create with message records reason in reflog' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git refs create --message="create reason" refs/heads/foo $A &&
		git reflog show refs/heads/foo >actual &&
		test_grep "create reason$" actual
	)
'

test_expect_success 'create with symref target creates target reference' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git symbolic-ref refs/heads/symref refs/heads/target &&
		git refs create refs/heads/symref $A &&
		git reflog exists refs/heads/target
	)
'

test_expect_success 'create with symref target and --no-deref refuses to create reference' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		git symbolic-ref refs/heads/symref refs/heads/target &&
		test_must_fail git refs create --no-deref refs/heads/symref $A 2>err &&
		test_grep "dangling symref already exists" err &&
		test_must_fail git reflog exists refs/heads/target
	)
'

test_expect_success 'create with empty message fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	(
		cd repo &&
		A=$(git rev-parse A) &&
		test_must_fail git refs create --message= refs/heads/foo $A 2>err &&
		test_grep "empty message" err &&
		test_must_fail git refs exists refs/heads/foo
	)
'

test_expect_success 'create without arguments fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	test_must_fail git -C repo refs create 2>err &&
	test_grep "requires reference name" err
'

test_expect_success 'create with too many arguments fails' '
	test_when_finished "rm -rf repo" &&
	setup_repo repo &&
	test_must_fail git -C repo refs create refs/heads/foo a b 2>err &&
	test_grep "requires reference name" err
'

test_done
