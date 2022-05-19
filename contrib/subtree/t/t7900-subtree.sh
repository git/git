#!/bin/sh
#
# Copyright (c) 2012 Avery Pennaraum
# Copyright (c) 2015 Alexey Shumkin
#
test_description='Basic porcelain support for subtrees

This test verifies the basic operation of the add, merge, split, pull,
and push subcommands of but subtree.
'

TEST_DIRECTORY=$(pwd)/../../../t
. "$TEST_DIRECTORY"/test-lib.sh

# Use our own wrapper around test-lib.sh's test_create_repo, in order
# to set log.date=relative.  `but subtree` parses the output of `but
# log`, and so it must be careful to not be affected by settings that
# change the `but log` output.  We test this by setting
# log.date=relative for every repo in the tests.
subtree_test_create_repo () {
	test_create_repo "$1" &&
	but -C "$1" config log.date relative
}

test_create_cummit () (
	repo=$1 &&
	cummit=$2 &&
	cd "$repo" &&
	mkdir -p "$(dirname "$cummit")" \
	|| error "Could not create directory for cummit"
	echo "$cummit" >"$cummit" &&
	but add "$cummit" || error "Could not add cummit"
	but cummit -m "$cummit" || error "Could not cummit"
)

test_wrong_flag() {
	test_must_fail "$@" >out 2>err &&
	test_must_be_empty out &&
	grep "flag does not make sense with" err
}

last_cummit_subject () {
	but log --pretty=format:%s -1
}

test_expect_success 'shows short help text for -h' '
	test_expect_code 129 but subtree -h >out 2>err &&
	test_must_be_empty err &&
	grep -e "^ *or: but subtree pull" out &&
	grep -e --annotate out
'

#
# Tests for 'but subtree add'
#

test_expect_success 'no merge from non-existent subtree' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		test_must_fail but subtree merge --prefix="sub dir" FETCH_HEAD
	)
'

test_expect_success 'no pull from non-existent subtree' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		test_must_fail but subtree pull --prefix="sub dir" ./"sub proj" HEAD
	)
'

test_expect_success 'add rejects flags for split' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		test_wrong_flag but subtree add --prefix="sub dir" --annotate=foo FETCH_HEAD &&
		test_wrong_flag but subtree add --prefix="sub dir" --branch=foo FETCH_HEAD &&
		test_wrong_flag but subtree add --prefix="sub dir" --ignore-joins FETCH_HEAD &&
		test_wrong_flag but subtree add --prefix="sub dir" --onto=foo FETCH_HEAD &&
		test_wrong_flag but subtree add --prefix="sub dir" --rejoin FETCH_HEAD
	)
'

test_expect_success 'add subproj as subtree into sub dir/ with --prefix' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD &&
		test "$(last_cummit_subject)" = "Add '\''sub dir/'\'' from cummit '\''$(but rev-parse FETCH_HEAD)'\''"
	)
'

test_expect_success 'add subproj as subtree into sub dir/ with --prefix and --message' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" --message="Added subproject" FETCH_HEAD &&
		test "$(last_cummit_subject)" = "Added subproject"
	)
'

test_expect_success 'add subproj as subtree into sub dir/ with --prefix as -P and --message as -m' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add -P "sub dir" -m "Added subproject" FETCH_HEAD &&
		test "$(last_cummit_subject)" = "Added subproject"
	)
'

test_expect_success 'add subproj as subtree into sub dir/ with --squash and --prefix and --message' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" --message="Added subproject with squash" --squash FETCH_HEAD &&
		test "$(last_cummit_subject)" = "Added subproject with squash"
	)
'

#
# Tests for 'but subtree merge'
#

test_expect_success 'merge rejects flags for split' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		test_wrong_flag but subtree merge --prefix="sub dir" --annotate=foo FETCH_HEAD &&
		test_wrong_flag but subtree merge --prefix="sub dir" --branch=foo FETCH_HEAD &&
		test_wrong_flag but subtree merge --prefix="sub dir" --ignore-joins FETCH_HEAD &&
		test_wrong_flag but subtree merge --prefix="sub dir" --onto=foo FETCH_HEAD &&
		test_wrong_flag but subtree merge --prefix="sub dir" --rejoin FETCH_HEAD
	)
'

test_expect_success 'merge new subproj history into sub dir/ with --prefix' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		test "$(last_cummit_subject)" = "Merge cummit '\''$(but rev-parse FETCH_HEAD)'\''"
	)
'

test_expect_success 'merge new subproj history into sub dir/ with --prefix and --message' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" --message="Merged changes from subproject" FETCH_HEAD &&
		test "$(last_cummit_subject)" = "Merged changes from subproject"
	)
'

test_expect_success 'merge new subproj history into sub dir/ with --squash and --prefix and --message' '
	subtree_test_create_repo "$test_count/sub proj" &&
	subtree_test_create_repo "$test_count" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" --message="Merged changes from subproject using squash" --squash FETCH_HEAD &&
		test "$(last_cummit_subject)" = "Merged changes from subproject using squash"
	)
'

test_expect_success 'merge the added subproj again, should do nothing' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD &&
		# this shouldn not actually do anything, since FETCH_HEAD
		# is already a parent
		result=$(but merge -s ours -m "merge -s -ours" FETCH_HEAD) &&
		test "${result}" = "Already up to date."
	)
'

test_expect_success 'merge new subproj history into subdir/ with a slash appended to the argument of --prefix' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/subproj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/subproj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./subproj HEAD &&
		but subtree add --prefix=subdir/ FETCH_HEAD
	) &&
	test_create_cummit "$test_count/subproj" sub2 &&
	(
		cd "$test_count" &&
		but fetch ./subproj HEAD &&
		but subtree merge --prefix=subdir/ FETCH_HEAD &&
		test "$(last_cummit_subject)" = "Merge cummit '\''$(but rev-parse FETCH_HEAD)'\''"
	)
'

#
# Tests for 'but subtree split'
#

test_expect_success 'split requires option --prefix' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD &&
		echo "You must provide the --prefix option." >expected &&
		test_must_fail but subtree split >actual 2>&1 &&
		test_debug "printf '"expected: "'" &&
		test_debug "cat expected" &&
		test_debug "printf '"actual: "'" &&
		test_debug "cat actual" &&
		test_cmp expected actual
	)
'

test_expect_success 'split requires path given by option --prefix must exist' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD &&
		echo "'\''non-existent-directory'\'' does not exist; use '\''but subtree add'\''" >expected &&
		test_must_fail but subtree split --prefix=non-existent-directory >actual 2>&1 &&
		test_debug "printf '"expected: "'" &&
		test_debug "cat expected" &&
		test_debug "printf '"actual: "'" &&
		test_debug "cat actual" &&
		test_cmp expected actual
	)
'

test_expect_success 'split rejects flags for add' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(but subtree split --prefix="sub dir" --annotate="*") &&
		test_wrong_flag but subtree split --prefix="sub dir" --squash &&
		test_wrong_flag but subtree split --prefix="sub dir" --message=foo
	)
'

test_expect_success 'split sub dir/ with --rejoin' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(but subtree split --prefix="sub dir" --annotate="*") &&
		but subtree split --prefix="sub dir" --annotate="*" --rejoin &&
		test "$(last_cummit_subject)" = "Split '\''sub dir/'\'' into cummit '\''$split_hash'\''"
	)
'

test_expect_success 'split sub dir/ with --rejoin from scratch' '
	subtree_test_create_repo "$test_count" &&
	test_create_cummit "$test_count" main1 &&
	(
		cd "$test_count" &&
		mkdir "sub dir" &&
		echo file >"sub dir"/file &&
		but add "sub dir/file" &&
		but cummit -m"sub dir file" &&
		split_hash=$(but subtree split --prefix="sub dir" --rejoin) &&
		but subtree split --prefix="sub dir" --rejoin &&
		test "$(last_cummit_subject)" = "Split '\''sub dir/'\'' into cummit '\''$split_hash'\''"
	)
'

test_expect_success 'split sub dir/ with --rejoin and --message' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		but subtree split --prefix="sub dir" --message="Split & rejoin" --annotate="*" --rejoin &&
		test "$(last_cummit_subject)" = "Split & rejoin"
	)
'

test_expect_success 'split "sub dir"/ with --rejoin and --squash' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" --squash FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but subtree pull --prefix="sub dir" --squash ./"sub proj" HEAD &&
		MAIN=$(but rev-parse --verify HEAD) &&
		SUB=$(but -C "sub proj" rev-parse --verify HEAD) &&

		SPLIT=$(but subtree split --prefix="sub dir" --annotate="*" --rejoin --squash) &&

		test_must_fail but merge-base --is-ancestor $SUB HEAD &&
		test_must_fail but merge-base --is-ancestor $SPLIT HEAD &&
		but rev-list HEAD ^$MAIN >cummit-list &&
		test_line_count = 2 cummit-list &&
		test "$(but rev-parse --verify HEAD:)"           = "$(but rev-parse --verify $MAIN:)" &&
		test "$(but rev-parse --verify HEAD:"sub dir")"  = "$(but rev-parse --verify $SPLIT:)" &&
		test "$(but rev-parse --verify HEAD^1)"          = $MAIN &&
		test "$(but rev-parse --verify HEAD^2)"         != $SPLIT &&
		test "$(but rev-parse --verify HEAD^2:)"         = "$(but rev-parse --verify $SPLIT:)" &&
		test "$(last_cummit_subject)" = "Split '\''sub dir/'\'' into cummit '\''$SPLIT'\''"
	)
'

test_expect_success 'split then pull "sub dir"/ with --rejoin and --squash' '
	# 1. "add"
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	but -C "$test_count" subtree --prefix="sub dir" add --squash ./"sub proj" HEAD &&

	# 2. cummit from parent
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&

	# 3. "split --rejoin --squash"
	but -C "$test_count" subtree --prefix="sub dir" split --rejoin --squash &&

	# 4. "pull --squash"
	test_create_cummit "$test_count/sub proj" sub2 &&
	but -C "$test_count" subtree -d --prefix="sub dir" pull --squash ./"sub proj" HEAD &&

	test_must_fail but merge-base HEAD FETCH_HEAD
'

test_expect_success 'split "sub dir"/ with --branch' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(but subtree split --prefix="sub dir" --annotate="*") &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br &&
		test "$(but rev-parse subproj-br)" = "$split_hash"
	)
'

test_expect_success 'check hash of split' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(but subtree split --prefix="sub dir" --annotate="*") &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br &&
		test "$(but rev-parse subproj-br)" = "$split_hash" &&
		# Check hash of split
		new_hash=$(but rev-parse subproj-br^2) &&
		(
			cd ./"sub proj" &&
			subdir_hash=$(but rev-parse HEAD) &&
			test "$new_hash" = "$subdir_hash"
		)
	)
'

test_expect_success 'split "sub dir"/ with --branch for an existing branch' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but branch subproj-br FETCH_HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(but subtree split --prefix="sub dir" --annotate="*") &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br &&
		test "$(but rev-parse subproj-br)" = "$split_hash"
	)
'

test_expect_success 'split "sub dir"/ with --branch for an incompatible branch' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but branch init HEAD &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		test_must_fail but subtree split --prefix="sub dir" --branch init
	)
'

#
# Tests for 'but subtree pull'
#

test_expect_success 'pull requires option --prefix' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		test_must_fail but subtree pull ./"sub proj" HEAD >out 2>err &&

		echo "You must provide the --prefix option." >expected &&
		test_must_be_empty out &&
		test_cmp expected err
	)
'

test_expect_success 'pull requires path given by option --prefix must exist' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		test_must_fail but subtree pull --prefix="sub dir" ./"sub proj" HEAD >out 2>err &&

		echo "'\''sub dir'\'' does not exist; use '\''but subtree add'\''" >expected &&
		test_must_be_empty out &&
		test_cmp expected err
	)
'

test_expect_success 'pull basic operation' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		exp=$(but -C "sub proj" rev-parse --verify HEAD:) &&
		but subtree pull --prefix="sub dir" ./"sub proj" HEAD &&
		act=$(but rev-parse --verify HEAD:"sub dir") &&
		test "$act" = "$exp"
	)
'

test_expect_success 'pull rejects flags for split' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	(
		test_must_fail but subtree pull --prefix="sub dir" --annotate=foo ./"sub proj" HEAD &&
		test_must_fail but subtree pull --prefix="sub dir" --branch=foo ./"sub proj" HEAD &&
		test_must_fail but subtree pull --prefix="sub dir" --ignore-joins ./"sub proj" HEAD &&
		test_must_fail but subtree pull --prefix="sub dir" --onto=foo ./"sub proj" HEAD &&
		test_must_fail but subtree pull --prefix="sub dir" --rejoin ./"sub proj" HEAD
	)
'

#
# Tests for 'but subtree push'
#

test_expect_success 'push requires option --prefix' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD &&
		echo "You must provide the --prefix option." >expected &&
		test_must_fail but subtree push "./sub proj" from-mainline >actual 2>&1 &&
		test_debug "printf '"expected: "'" &&
		test_debug "cat expected" &&
		test_debug "printf '"actual: "'" &&
		test_debug "cat actual" &&
		test_cmp expected actual
	)
'

test_expect_success 'push requires path given by option --prefix must exist' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD &&
		echo "'\''non-existent-directory'\'' does not exist; use '\''but subtree add'\''" >expected &&
		test_must_fail but subtree push --prefix=non-existent-directory "./sub proj" from-mainline >actual 2>&1 &&
		test_debug "printf '"expected: "'" &&
		test_debug "cat expected" &&
		test_debug "printf '"actual: "'" &&
		test_debug "cat actual" &&
		test_cmp expected actual
	)
'

test_expect_success 'push rejects flags for add' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		test_wrong_flag but subtree split --prefix="sub dir" --squash ./"sub proj" from-mainline &&
		test_wrong_flag but subtree split --prefix="sub dir" --message=foo ./"sub proj" from-mainline
	)
'

test_expect_success 'push basic operation' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		before=$(but rev-parse --verify HEAD) &&
		split_hash=$(but subtree split --prefix="sub dir") &&
		but subtree push --prefix="sub dir" ./"sub proj" from-mainline &&
		test "$before" = "$(but rev-parse --verify HEAD)" &&
		test "$split_hash" = "$(but -C "sub proj" rev-parse --verify refs/heads/from-mainline)"
	)
'

test_expect_success 'push sub dir/ with --rejoin' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(but subtree split --prefix="sub dir" --annotate="*") &&
		but subtree push --prefix="sub dir" --annotate="*" --rejoin ./"sub proj" from-mainline &&
		test "$(last_cummit_subject)" = "Split '\''sub dir/'\'' into cummit '\''$split_hash'\''" &&
		test "$split_hash" = "$(but -C "sub proj" rev-parse --verify refs/heads/from-mainline)"
	)
'

test_expect_success 'push sub dir/ with --rejoin from scratch' '
	subtree_test_create_repo "$test_count" &&
	test_create_cummit "$test_count" main1 &&
	(
		cd "$test_count" &&
		mkdir "sub dir" &&
		echo file >"sub dir"/file &&
		but add "sub dir/file" &&
		but cummit -m"sub dir file" &&
		split_hash=$(but subtree split --prefix="sub dir" --rejoin) &&
		but init --bare "sub proj.but" &&
		but subtree push --prefix="sub dir" --rejoin ./"sub proj.but" from-mainline &&
		test "$(last_cummit_subject)" = "Split '\''sub dir/'\'' into cummit '\''$split_hash'\''" &&
		test "$split_hash" = "$(but -C "sub proj.but" rev-parse --verify refs/heads/from-mainline)"
	)
'

test_expect_success 'push sub dir/ with --rejoin and --message' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		but subtree push --prefix="sub dir" --message="Split & rejoin" --annotate="*" --rejoin ./"sub proj" from-mainline &&
		test "$(last_cummit_subject)" = "Split & rejoin" &&
		split_hash="$(but rev-parse --verify HEAD^2)" &&
		test "$split_hash" = "$(but -C "sub proj" rev-parse --verify refs/heads/from-mainline)"
	)
'

test_expect_success 'push "sub dir"/ with --rejoin and --squash' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" --squash FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but subtree pull --prefix="sub dir" --squash ./"sub proj" HEAD &&
		MAIN=$(but rev-parse --verify HEAD) &&
		SUB=$(but -C "sub proj" rev-parse --verify HEAD) &&

		SPLIT=$(but subtree split --prefix="sub dir" --annotate="*") &&
		but subtree push --prefix="sub dir" --annotate="*" --rejoin --squash ./"sub proj" from-mainline &&

		test_must_fail but merge-base --is-ancestor $SUB HEAD &&
		test_must_fail but merge-base --is-ancestor $SPLIT HEAD &&
		but rev-list HEAD ^$MAIN >cummit-list &&
		test_line_count = 2 cummit-list &&
		test "$(but rev-parse --verify HEAD:)"           = "$(but rev-parse --verify $MAIN:)" &&
		test "$(but rev-parse --verify HEAD:"sub dir")"  = "$(but rev-parse --verify $SPLIT:)" &&
		test "$(but rev-parse --verify HEAD^1)"          = $MAIN &&
		test "$(but rev-parse --verify HEAD^2)"         != $SPLIT &&
		test "$(but rev-parse --verify HEAD^2:)"         = "$(but rev-parse --verify $SPLIT:)" &&
		test "$(last_cummit_subject)" = "Split '\''sub dir/'\'' into cummit '\''$SPLIT'\''" &&
		test "$SPLIT" = "$(but -C "sub proj" rev-parse --verify refs/heads/from-mainline)"
	)
'

test_expect_success 'push "sub dir"/ with --branch' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(but subtree split --prefix="sub dir" --annotate="*") &&
		but subtree push --prefix="sub dir" --annotate="*" --branch subproj-br ./"sub proj" from-mainline &&
		test "$(but rev-parse subproj-br)" = "$split_hash" &&
		test "$split_hash" = "$(but -C "sub proj" rev-parse --verify refs/heads/from-mainline)"
	)
'

test_expect_success 'check hash of push' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(but subtree split --prefix="sub dir" --annotate="*") &&
		but subtree push --prefix="sub dir" --annotate="*" --branch subproj-br ./"sub proj" from-mainline &&
		test "$(but rev-parse subproj-br)" = "$split_hash" &&
		# Check hash of split
		new_hash=$(but rev-parse subproj-br^2) &&
		(
			cd ./"sub proj" &&
			subdir_hash=$(but rev-parse HEAD) &&
			test "$new_hash" = "$subdir_hash"
		) &&
		test "$split_hash" = "$(but -C "sub proj" rev-parse --verify refs/heads/from-mainline)"
	)
'

test_expect_success 'push "sub dir"/ with --branch for an existing branch' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but branch subproj-br FETCH_HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(but subtree split --prefix="sub dir" --annotate="*") &&
		but subtree push --prefix="sub dir" --annotate="*" --branch subproj-br ./"sub proj" from-mainline &&
		test "$(but rev-parse subproj-br)" = "$split_hash" &&
		test "$split_hash" = "$(but -C "sub proj" rev-parse --verify refs/heads/from-mainline)"
	)
'

test_expect_success 'push "sub dir"/ with --branch for an incompatible branch' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but branch init HEAD &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		test_must_fail but subtree push --prefix="sub dir" --branch init "./sub proj" from-mainline
	)
'

test_expect_success 'push "sub dir"/ with a local rev' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		bad_tree=$(but rev-parse --verify HEAD:"sub dir") &&
		good_tree=$(but rev-parse --verify HEAD^:"sub dir") &&
		but subtree push --prefix="sub dir" --annotate="*" ./"sub proj" HEAD^:from-mainline &&
		split_tree=$(but -C "sub proj" rev-parse --verify refs/heads/from-mainline:) &&
		test "$split_tree" = "$good_tree"
	)
'

#
# Validity checking
#

test_expect_success 'make sure exactly the right set of files ends up in the subproj' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_cummit "$test_count/sub proj" sub3 &&
	test_create_cummit "$test_count" "sub dir"/main-sub3 &&
	(
		cd "$test_count/sub proj" &&
		but fetch .. subproj-br &&
		but merge FETCH_HEAD
	) &&
	test_create_cummit "$test_count/sub proj" sub4 &&
	(
		cd "$test_count" &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub4 &&
	(
		cd "$test_count" &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$test_count/sub proj" &&
		but fetch .. subproj-br &&
		but merge FETCH_HEAD &&

		test_write_lines main-sub1 main-sub2 main-sub3 main-sub4 \
			sub1 sub2 sub3 sub4 >expect &&
		but ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'make sure the subproj *only* contains cummits that affect the "sub dir"' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_cummit "$test_count/sub proj" sub3 &&
	test_create_cummit "$test_count" "sub dir"/main-sub3 &&
	(
		cd "$test_count/sub proj" &&
		but fetch .. subproj-br &&
		but merge FETCH_HEAD
	) &&
	test_create_cummit "$test_count/sub proj" sub4 &&
	(
		cd "$test_count" &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub4 &&
	(
		cd "$test_count" &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$test_count/sub proj" &&
		but fetch .. subproj-br &&
		but merge FETCH_HEAD &&

		test_write_lines main-sub1 main-sub2 main-sub3 main-sub4 \
			sub1 sub2 sub3 sub4 >expect &&
		but log --name-only --pretty=format:"" >log &&
		sort <log | sed "/^\$/ d" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'make sure exactly the right set of files ends up in the mainline' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_cummit "$test_count/sub proj" sub3 &&
	test_create_cummit "$test_count" "sub dir"/main-sub3 &&
	(
		cd "$test_count/sub proj" &&
		but fetch .. subproj-br &&
		but merge FETCH_HEAD
	) &&
	test_create_cummit "$test_count/sub proj" sub4 &&
	(
		cd "$test_count" &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub4 &&
	(
		cd "$test_count" &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$test_count/sub proj" &&
		but fetch .. subproj-br &&
		but merge FETCH_HEAD
	) &&
	(
		cd "$test_count" &&
		but subtree pull --prefix="sub dir" ./"sub proj" HEAD &&

		test_write_lines main1 main2 >chkm &&
		test_write_lines main-sub1 main-sub2 main-sub3 main-sub4 >chkms &&
		sed "s,^,sub dir/," chkms >chkms_sub &&
		test_write_lines sub1 sub2 sub3 sub4 >chks &&
		sed "s,^,sub dir/," chks >chks_sub &&

		cat chkm chkms_sub chks_sub >expect &&
		but ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'make sure each filename changed exactly once in the entire history' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but config log.date relative &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_cummit "$test_count/sub proj" sub3 &&
	test_create_cummit "$test_count" "sub dir"/main-sub3 &&
	(
		cd "$test_count/sub proj" &&
		but fetch .. subproj-br &&
		but merge FETCH_HEAD
	) &&
	test_create_cummit "$test_count/sub proj" sub4 &&
	(
		cd "$test_count" &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub4 &&
	(
		cd "$test_count" &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$test_count/sub proj" &&
		but fetch .. subproj-br &&
		but merge FETCH_HEAD
	) &&
	(
		cd "$test_count" &&
		but subtree pull --prefix="sub dir" ./"sub proj" HEAD &&

		test_write_lines main1 main2 >chkm &&
		test_write_lines sub1 sub2 sub3 sub4 >chks &&
		test_write_lines main-sub1 main-sub2 main-sub3 main-sub4 >chkms &&
		sed "s,^,sub dir/," chkms >chkms_sub &&

		# main-sub?? and /"sub dir"/main-sub?? both change, because those are the
		# changes that were split into their own history.  And "sub dir"/sub?? never
		# change, since they were *only* changed in the subtree branch.
		but log --name-only --pretty=format:"" >log &&
		sort <log >sorted-log &&
		sed "/^$/ d" sorted-log >actual &&

		cat chkms chkm chks chkms_sub >expect-unsorted &&
		sort expect-unsorted >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'make sure the --rejoin cummits never make it into subproj' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_cummit "$test_count/sub proj" sub3 &&
	test_create_cummit "$test_count" "sub dir"/main-sub3 &&
	(
		cd "$test_count/sub proj" &&
		but fetch .. subproj-br &&
		but merge FETCH_HEAD
	) &&
	test_create_cummit "$test_count/sub proj" sub4 &&
	(
		cd "$test_count" &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub4 &&
	(
		cd "$test_count" &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$test_count/sub proj" &&
		but fetch .. subproj-br &&
		but merge FETCH_HEAD
	) &&
	(
		cd "$test_count" &&
		but subtree pull --prefix="sub dir" ./"sub proj" HEAD &&
		test "$(but log --pretty=format:"%s" HEAD^2 | grep -i split)" = ""
	)
'

test_expect_success 'make sure no "but subtree" tagged cummits make it into subproj' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_cummit "$test_count/sub proj" sub3 &&
	test_create_cummit "$test_count" "sub dir"/main-sub3 &&
	(
		cd "$test_count/sub proj" &&
		but fetch .. subproj-br &&
		 but merge FETCH_HEAD
	) &&
	test_create_cummit "$test_count/sub proj" sub4 &&
	(
		cd "$test_count" &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub4 &&
	(
		cd "$test_count" &&
		but subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$test_count/sub proj" &&
		but fetch .. subproj-br &&
		but merge FETCH_HEAD
	) &&
	(
		cd "$test_count" &&
		but subtree pull --prefix="sub dir" ./"sub proj" HEAD &&

		# They are meaningless to subproj since one side of the merge refers to the mainline
		test "$(but log --pretty=format:"%s%n%b" HEAD^2 | grep "but-subtree.*:")" = ""
	)
'

#
# A new set of tests
#

test_expect_success 'make sure "but subtree split" find the correct parent' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but branch subproj-ref FETCH_HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	(
		cd "$test_count" &&
		but subtree split --prefix="sub dir" --branch subproj-br &&

		# at this point, the new cummit parent should be subproj-ref, if it is
		# not, something went wrong (the "newparent" of "HEAD~" cummit should
		# have been sub2, but it was not, because its cache was not set to
		# itself)
		test "$(but log --pretty=format:%P -1 subproj-br)" = "$(but rev-parse subproj-ref)"
	)
'

test_expect_success 'split a new subtree without --onto option' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	(
		cd "$test_count" &&
		but subtree split --prefix="sub dir" --branch subproj-br
	) &&
	mkdir "$test_count"/"sub dir2" &&
	test_create_cummit "$test_count" "sub dir2"/main-sub2 &&
	(
		cd "$test_count" &&

		# also test that we still can split out an entirely new subtree
		# if the parent of the first cummit in the tree is not empty,
		# then the new subtree has accidentally been attached to something
		but subtree split --prefix="sub dir2" --branch subproj2-br &&
		test "$(but log --pretty=format:%P -1 subproj2-br)" = ""
	)
'

test_expect_success 'verify one file change per cummit' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but branch sub1 FETCH_HEAD &&
		but subtree add --prefix="sub dir" sub1
	) &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	(
		cd "$test_count" &&
		but subtree split --prefix="sub dir" --branch subproj-br
	) &&
	mkdir "$test_count"/"sub dir2" &&
	test_create_cummit "$test_count" "sub dir2"/main-sub2 &&
	(
		cd "$test_count" &&
		but subtree split --prefix="sub dir2" --branch subproj2-br &&

		but log --format="%H" >cummit-list &&
		while read cummit
		do
			but log -n1 --format="" --name-only "$cummit" >file-list &&
			test_line_count -le 1 file-list || return 1
		done <cummit-list
	)
'

test_expect_success 'push split to subproj' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_cummit "$test_count" main1 &&
	test_create_cummit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		but fetch ./"sub proj" HEAD &&
		but subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub1 &&
	test_create_cummit "$test_count" main2 &&
	test_create_cummit "$test_count/sub proj" sub2 &&
	test_create_cummit "$test_count" "sub dir"/main-sub2 &&
	(
		cd $test_count/"sub proj" &&
		but branch sub-branch-1 &&
		cd .. &&
		but fetch ./"sub proj" HEAD &&
		but subtree merge --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_cummit "$test_count" "sub dir"/main-sub3 &&
	(
		cd "$test_count" &&
		but subtree push ./"sub proj" --prefix "sub dir" sub-branch-1 &&
		cd ./"sub proj" &&
		but checkout sub-branch-1 &&
		test "$(last_cummit_subject)" = "sub dir/main-sub3"
	)
'

#
# This test covers 2 cases in subtree split copy_or_skip code
# 1) Merges where one parent is a superset of the changes of the other
#    parent regarding changes to the subtree, in this case the merge
#    cummit should be copied
# 2) Merges where only one parent operate on the subtree, and the merge
#    cummit should be skipped
#
# (1) is checked by ensuring subtree_tip is a descendent of subtree_branch
# (2) should have a check added (not_a_subtree_change shouldn't be present
#     on the produced subtree)
#
# Other related cases which are not tested (or currently handled correctly)
# - Case (1) where there are more than 2 parents, it will sometimes correctly copy
#   the merge, and sometimes not
# - Merge cummit where both parents have same tree as the merge, currently
#   will always be skipped, even if they reached that state via different
#   set of cummits.
#

test_expect_success 'subtree descendant check' '
	subtree_test_create_repo "$test_count" &&
	defaultBranch=$(sed "s,ref: refs/heads/,," "$test_count/.but/HEAD") &&
	test_create_cummit "$test_count" folder_subtree/a &&
	(
		cd "$test_count" &&
		but branch branch
	) &&
	test_create_cummit "$test_count" folder_subtree/0 &&
	test_create_cummit "$test_count" folder_subtree/b &&
	cherry=$(cd "$test_count" && but rev-parse HEAD) &&
	(
		cd "$test_count" &&
		but checkout branch
	) &&
	test_create_cummit "$test_count" cummit_on_branch &&
	(
		cd "$test_count" &&
		but cherry-pick $cherry &&
		but checkout $defaultBranch &&
		but merge -m "merge should be kept on subtree" branch &&
		but branch no_subtree_work_branch
	) &&
	test_create_cummit "$test_count" folder_subtree/d &&
	(
		cd "$test_count" &&
		but checkout no_subtree_work_branch
	) &&
	test_create_cummit "$test_count" not_a_subtree_change &&
	(
		cd "$test_count" &&
		but checkout $defaultBranch &&
		but merge -m "merge should be skipped on subtree" no_subtree_work_branch &&

		but subtree split --prefix folder_subtree/ --branch subtree_tip $defaultBranch &&
		but subtree split --prefix folder_subtree/ --branch subtree_branch branch &&
		test $(but rev-list --count subtree_tip..subtree_branch) = 0
	)
'

test_done
