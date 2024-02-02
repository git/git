#!/bin/sh
#
# Copyright (c) 2012 Avery Pennaraum
# Copyright (c) 2015 Alexey Shumkin
#
test_description='Basic porcelain support for subtrees

This test verifies the basic operation of the add, merge, split, pull,
and push subcommands of git subtree.
'

TEST_DIRECTORY=$(pwd)/../../../t
. "$TEST_DIRECTORY"/test-lib.sh

# Use our own wrapper around test-lib.sh's test_create_repo, in order
# to set log.date=relative.  `git subtree` parses the output of `git
# log`, and so it must be careful to not be affected by settings that
# change the `git log` output.  We test this by setting
# log.date=relative for every repo in the tests.
subtree_test_create_repo () {
	test_create_repo "$1" &&
	git -C "$1" config log.date relative
}

test_create_commit () (
	repo=$1 &&
	commit=$2 &&
	cd "$repo" &&
	mkdir -p "$(dirname "$commit")" \
	|| error "Could not create directory for commit"
	echo "$commit" >"$commit" &&
	git add "$commit" || error "Could not add commit"
	git commit -m "$commit" || error "Could not commit"
)

test_wrong_flag() {
	test_must_fail "$@" >out 2>err &&
	test_must_be_empty out &&
	grep "flag does not make sense with" err
}

last_commit_subject () {
	git log --pretty=format:%s -1
}

# Upon 'git subtree add|merge --squash' of an annotated tag,
# pre-2.32.0 versions of 'git subtree' would write the hash of the tag
# (sub1 below), instead of the commit (sub1^{commit}) in the
# "git-subtree-split" trailer.
# We immitate this behaviour below using a replace ref.
# This function creates 3 repositories:
# - $1
# - $1-sub (added as subtree "sub" in $1)
# - $1-clone (clone of $1)
test_create_pre2_32_repo () {
	subtree_test_create_repo "$1" &&
	subtree_test_create_repo "$1-sub" &&
	test_commit -C "$1" main1 &&
	test_commit -C "$1-sub" --annotate sub1 &&
	git -C "$1" subtree add --prefix="sub" --squash "../$1-sub" sub1 &&
	tag=$(git -C "$1" rev-parse FETCH_HEAD) &&
	commit=$(git -C "$1" rev-parse FETCH_HEAD^{commit}) &&
	git -C "$1" log -1 --format=%B HEAD^2 >msg &&
	test_commit -C "$1-sub" --annotate sub2 &&
	git clone --no-local "$1" "$1-clone" &&
	new_commit=$(cat msg | sed -e "s/$commit/$tag/" | git -C "$1-clone" commit-tree HEAD^2^{tree}) &&
	git -C "$1-clone" replace HEAD^2 $new_commit
}

test_expect_success 'shows short help text for -h' '
	test_expect_code 129 git subtree -h >out 2>err &&
	test_must_be_empty err &&
	grep -e "^ *or: git subtree pull" out &&
	grep -F -e "--[no-]annotate" out
'

#
# Tests for 'git subtree add'
#

test_expect_success 'no merge from non-existent subtree' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		test_must_fail git subtree merge --prefix="sub dir" FETCH_HEAD
	)
'

test_expect_success 'no pull from non-existent subtree' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		test_must_fail git subtree pull --prefix="sub dir" ./"sub proj" HEAD
	)
'

test_expect_success 'add rejects flags for split' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		test_wrong_flag git subtree add --prefix="sub dir" --annotate=foo FETCH_HEAD &&
		test_wrong_flag git subtree add --prefix="sub dir" --branch=foo FETCH_HEAD &&
		test_wrong_flag git subtree add --prefix="sub dir" --ignore-joins FETCH_HEAD &&
		test_wrong_flag git subtree add --prefix="sub dir" --onto=foo FETCH_HEAD &&
		test_wrong_flag git subtree add --prefix="sub dir" --rejoin FETCH_HEAD
	)
'

test_expect_success 'add subproj as subtree into sub dir/ with --prefix' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD &&
		test "$(last_commit_subject)" = "Add '\''sub dir/'\'' from commit '\''$(git rev-parse FETCH_HEAD)'\''"
	)
'

test_expect_success 'add subproj as subtree into sub dir/ with --prefix and --message' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" --message="Added subproject" FETCH_HEAD &&
		test "$(last_commit_subject)" = "Added subproject"
	)
'

test_expect_success 'add subproj as subtree into sub dir/ with --prefix as -P and --message as -m' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add -P "sub dir" -m "Added subproject" FETCH_HEAD &&
		test "$(last_commit_subject)" = "Added subproject"
	)
'

test_expect_success 'add subproj as subtree into sub dir/ with --squash and --prefix and --message' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" --message="Added subproject with squash" --squash FETCH_HEAD &&
		test "$(last_commit_subject)" = "Added subproject with squash"
	)
'

#
# Tests for 'git subtree merge'
#

test_expect_success 'merge rejects flags for split' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		test_wrong_flag git subtree merge --prefix="sub dir" --annotate=foo FETCH_HEAD &&
		test_wrong_flag git subtree merge --prefix="sub dir" --branch=foo FETCH_HEAD &&
		test_wrong_flag git subtree merge --prefix="sub dir" --ignore-joins FETCH_HEAD &&
		test_wrong_flag git subtree merge --prefix="sub dir" --onto=foo FETCH_HEAD &&
		test_wrong_flag git subtree merge --prefix="sub dir" --rejoin FETCH_HEAD
	)
'

test_expect_success 'merge new subproj history into sub dir/ with --prefix' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		test "$(last_commit_subject)" = "Merge commit '\''$(git rev-parse FETCH_HEAD)'\''"
	)
'

test_expect_success 'merge new subproj history into sub dir/ with --prefix and --message' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" --message="Merged changes from subproject" FETCH_HEAD &&
		test "$(last_commit_subject)" = "Merged changes from subproject"
	)
'

test_expect_success 'merge new subproj history into sub dir/ with --squash and --prefix and --message' '
	subtree_test_create_repo "$test_count/sub proj" &&
	subtree_test_create_repo "$test_count" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" --message="Merged changes from subproject using squash" --squash FETCH_HEAD &&
		test "$(last_commit_subject)" = "Merged changes from subproject using squash"
	)
'

test_expect_success 'merge the added subproj again, should do nothing' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD &&
		# this shouldn not actually do anything, since FETCH_HEAD
		# is already a parent
		result=$(git merge -s ours -m "merge -s -ours" FETCH_HEAD) &&
		test "${result}" = "Already up to date."
	)
'

test_expect_success 'merge new subproj history into subdir/ with a slash appended to the argument of --prefix' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/subproj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/subproj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./subproj HEAD &&
		git subtree add --prefix=subdir/ FETCH_HEAD
	) &&
	test_create_commit "$test_count/subproj" sub2 &&
	(
		cd "$test_count" &&
		git fetch ./subproj HEAD &&
		git subtree merge --prefix=subdir/ FETCH_HEAD &&
		test "$(last_commit_subject)" = "Merge commit '\''$(git rev-parse FETCH_HEAD)'\''"
	)
'

test_expect_success 'merge with --squash after annotated tag was added/merged with --squash pre-v2.32.0 ' '
	test_create_pre2_32_repo "$test_count" &&
	git -C "$test_count-clone" fetch "../$test_count-sub" sub2  &&
	test_must_fail git -C "$test_count-clone" subtree merge --prefix="sub" --squash FETCH_HEAD &&
	git -C "$test_count-clone" subtree merge --prefix="sub" --squash FETCH_HEAD  "../$test_count-sub"
'

#
# Tests for 'git subtree split'
#

test_expect_success 'split requires option --prefix' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD &&
		echo "fatal: you must provide the --prefix option." >expected &&
		test_must_fail git subtree split >actual 2>&1 &&
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
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD &&
		echo "fatal: '\''non-existent-directory'\'' does not exist; use '\''git subtree add'\''" >expected &&
		test_must_fail git subtree split --prefix=non-existent-directory >actual 2>&1 &&
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
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(git subtree split --prefix="sub dir" --annotate="*") &&
		test_wrong_flag git subtree split --prefix="sub dir" --squash &&
		test_wrong_flag git subtree split --prefix="sub dir" --message=foo
	)
'

test_expect_success 'split sub dir/ with --rejoin' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(git subtree split --prefix="sub dir" --annotate="*") &&
		git subtree split --prefix="sub dir" --annotate="*" --rejoin &&
		test "$(last_commit_subject)" = "Split '\''sub dir/'\'' into commit '\''$split_hash'\''"
	)
'

# Tests that commits from other subtrees are not processed as
# part of a split.
#
# This test performs the following:
# - Creates Repo with subtrees 'subA' and 'subB'
# - Creates commits in the repo including changes to subtrees
# - Runs the following 'split' and commit' commands in order:
# 	- Perform 'split' on subtree A
# 	- Perform 'split' on subtree B
# 	- Create new commits with changes to subtree A and B
# 	- Perform split on subtree A
# 	- Check that the commits in subtree B are not processed
#			as part of the subtree A split
test_expect_success 'split with multiple subtrees' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/subA" &&
	subtree_test_create_repo "$test_count/subB" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/subA" subA1 &&
	test_create_commit "$test_count/subA" subA2 &&
	test_create_commit "$test_count/subA" subA3 &&
	test_create_commit "$test_count/subB" subB1 &&
	git -C "$test_count" fetch ./subA HEAD &&
	git -C "$test_count" subtree add --prefix=subADir FETCH_HEAD &&
	git -C "$test_count" fetch ./subB HEAD &&
	git -C "$test_count" subtree add --prefix=subBDir FETCH_HEAD &&
	test_create_commit "$test_count" subADir/main-subA1 &&
	test_create_commit "$test_count" subBDir/main-subB1 &&
	git -C "$test_count" subtree split --prefix=subADir \
		--squash --rejoin -m "Sub A Split 1" &&
	git -C "$test_count" subtree split --prefix=subBDir \
		--squash --rejoin -m "Sub B Split 1" &&
	test_create_commit "$test_count" subADir/main-subA2 &&
	test_create_commit "$test_count" subBDir/main-subB2 &&
	git -C "$test_count" subtree split --prefix=subADir \
		--squash --rejoin -m "Sub A Split 2" &&
	test "$(git -C "$test_count" subtree split --prefix=subBDir \
		--squash --rejoin -d -m "Sub B Split 1" 2>&1 | grep -w "\[1\]")" = ""
'

test_expect_success 'split sub dir/ with --rejoin from scratch' '
	subtree_test_create_repo "$test_count" &&
	test_create_commit "$test_count" main1 &&
	(
		cd "$test_count" &&
		mkdir "sub dir" &&
		echo file >"sub dir"/file &&
		git add "sub dir/file" &&
		git commit -m"sub dir file" &&
		split_hash=$(git subtree split --prefix="sub dir" --rejoin) &&
		git subtree split --prefix="sub dir" --rejoin &&
		test "$(last_commit_subject)" = "Split '\''sub dir/'\'' into commit '\''$split_hash'\''"
	)
'

test_expect_success 'split sub dir/ with --rejoin and --message' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		git subtree split --prefix="sub dir" --message="Split & rejoin" --annotate="*" --rejoin &&
		test "$(last_commit_subject)" = "Split & rejoin"
	)
'

test_expect_success 'split "sub dir"/ with --rejoin and --squash' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" --squash FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git subtree pull --prefix="sub dir" --squash ./"sub proj" HEAD &&
		MAIN=$(git rev-parse --verify HEAD) &&
		SUB=$(git -C "sub proj" rev-parse --verify HEAD) &&

		SPLIT=$(git subtree split --prefix="sub dir" --annotate="*" --rejoin --squash) &&

		test_must_fail git merge-base --is-ancestor $SUB HEAD &&
		test_must_fail git merge-base --is-ancestor $SPLIT HEAD &&
		git rev-list HEAD ^$MAIN >commit-list &&
		test_line_count = 2 commit-list &&
		test "$(git rev-parse --verify HEAD:)"           = "$(git rev-parse --verify $MAIN:)" &&
		test "$(git rev-parse --verify HEAD:"sub dir")"  = "$(git rev-parse --verify $SPLIT:)" &&
		test "$(git rev-parse --verify HEAD^1)"          = $MAIN &&
		test "$(git rev-parse --verify HEAD^2)"         != $SPLIT &&
		test "$(git rev-parse --verify HEAD^2:)"         = "$(git rev-parse --verify $SPLIT:)" &&
		test "$(last_commit_subject)" = "Split '\''sub dir/'\'' into commit '\''$SPLIT'\''"
	)
'

test_expect_success 'split then pull "sub dir"/ with --rejoin and --squash' '
	# 1. "add"
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	git -C "$test_count" subtree --prefix="sub dir" add --squash ./"sub proj" HEAD &&

	# 2. commit from parent
	test_create_commit "$test_count" "sub dir"/main-sub1 &&

	# 3. "split --rejoin --squash"
	git -C "$test_count" subtree --prefix="sub dir" split --rejoin --squash &&

	# 4. "pull --squash"
	test_create_commit "$test_count/sub proj" sub2 &&
	git -C "$test_count" subtree -d --prefix="sub dir" pull --squash ./"sub proj" HEAD &&

	test_must_fail git merge-base HEAD FETCH_HEAD
'

test_expect_success 'split "sub dir"/ with --branch' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(git subtree split --prefix="sub dir" --annotate="*") &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br &&
		test "$(git rev-parse subproj-br)" = "$split_hash"
	)
'

test_expect_success 'check hash of split' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(git subtree split --prefix="sub dir" --annotate="*") &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br &&
		test "$(git rev-parse subproj-br)" = "$split_hash" &&
		# Check hash of split
		new_hash=$(git rev-parse subproj-br^2) &&
		(
			cd ./"sub proj" &&
			subdir_hash=$(git rev-parse HEAD) &&
			test "$new_hash" = "$subdir_hash"
		)
	)
'

test_expect_success 'split "sub dir"/ with --branch for an existing branch' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git branch subproj-br FETCH_HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(git subtree split --prefix="sub dir" --annotate="*") &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br &&
		test "$(git rev-parse subproj-br)" = "$split_hash"
	)
'

test_expect_success 'split "sub dir"/ with --branch for an incompatible branch' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git branch init HEAD &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		test_must_fail git subtree split --prefix="sub dir" --branch init
	)
'

test_expect_success 'split after annotated tag was added/merged with --squash pre-v2.32.0' '
	test_create_pre2_32_repo "$test_count" &&
	test_must_fail git -C "$test_count-clone" subtree split --prefix="sub" HEAD &&
	git -C "$test_count-clone" subtree split --prefix="sub" HEAD "../$test_count-sub"
'

#
# Tests for 'git subtree pull'
#

test_expect_success 'pull requires option --prefix' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		test_must_fail git subtree pull ./"sub proj" HEAD >out 2>err &&

		echo "fatal: you must provide the --prefix option." >expected &&
		test_must_be_empty out &&
		test_cmp expected err
	)
'

test_expect_success 'pull requires path given by option --prefix must exist' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		test_must_fail git subtree pull --prefix="sub dir" ./"sub proj" HEAD >out 2>err &&

		echo "fatal: '\''sub dir'\'' does not exist; use '\''git subtree add'\''" >expected &&
		test_must_be_empty out &&
		test_cmp expected err
	)
'

test_expect_success 'pull basic operation' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		exp=$(git -C "sub proj" rev-parse --verify HEAD:) &&
		git subtree pull --prefix="sub dir" ./"sub proj" HEAD &&
		act=$(git rev-parse --verify HEAD:"sub dir") &&
		test "$act" = "$exp"
	)
'

test_expect_success 'pull rejects flags for split' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count/sub proj" sub2 &&
	(
		test_must_fail git subtree pull --prefix="sub dir" --annotate=foo ./"sub proj" HEAD &&
		test_must_fail git subtree pull --prefix="sub dir" --branch=foo ./"sub proj" HEAD &&
		test_must_fail git subtree pull --prefix="sub dir" --ignore-joins ./"sub proj" HEAD &&
		test_must_fail git subtree pull --prefix="sub dir" --onto=foo ./"sub proj" HEAD &&
		test_must_fail git subtree pull --prefix="sub dir" --rejoin ./"sub proj" HEAD
	)
'

test_expect_success 'pull with --squash after annotated tag was added/merged with --squash pre-v2.32.0 ' '
	test_create_pre2_32_repo "$test_count" &&
	git -C "$test_count-clone" subtree -d pull --prefix="sub" --squash "../$test_count-sub" sub2
'

#
# Tests for 'git subtree push'
#

test_expect_success 'push requires option --prefix' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD &&
		echo "fatal: you must provide the --prefix option." >expected &&
		test_must_fail git subtree push "./sub proj" from-mainline >actual 2>&1 &&
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
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD &&
		echo "fatal: '\''non-existent-directory'\'' does not exist; use '\''git subtree add'\''" >expected &&
		test_must_fail git subtree push --prefix=non-existent-directory "./sub proj" from-mainline >actual 2>&1 &&
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
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		test_wrong_flag git subtree split --prefix="sub dir" --squash ./"sub proj" from-mainline &&
		test_wrong_flag git subtree split --prefix="sub dir" --message=foo ./"sub proj" from-mainline
	)
'

test_expect_success 'push basic operation' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		before=$(git rev-parse --verify HEAD) &&
		split_hash=$(git subtree split --prefix="sub dir") &&
		git subtree push --prefix="sub dir" ./"sub proj" from-mainline &&
		test "$before" = "$(git rev-parse --verify HEAD)" &&
		test "$split_hash" = "$(git -C "sub proj" rev-parse --verify refs/heads/from-mainline)"
	)
'

test_expect_success 'push sub dir/ with --rejoin' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(git subtree split --prefix="sub dir" --annotate="*") &&
		git subtree push --prefix="sub dir" --annotate="*" --rejoin ./"sub proj" from-mainline &&
		test "$(last_commit_subject)" = "Split '\''sub dir/'\'' into commit '\''$split_hash'\''" &&
		test "$split_hash" = "$(git -C "sub proj" rev-parse --verify refs/heads/from-mainline)"
	)
'

test_expect_success 'push sub dir/ with --rejoin from scratch' '
	subtree_test_create_repo "$test_count" &&
	test_create_commit "$test_count" main1 &&
	(
		cd "$test_count" &&
		mkdir "sub dir" &&
		echo file >"sub dir"/file &&
		git add "sub dir/file" &&
		git commit -m"sub dir file" &&
		split_hash=$(git subtree split --prefix="sub dir" --rejoin) &&
		git init --bare "sub proj.git" &&
		git subtree push --prefix="sub dir" --rejoin ./"sub proj.git" from-mainline &&
		test "$(last_commit_subject)" = "Split '\''sub dir/'\'' into commit '\''$split_hash'\''" &&
		test "$split_hash" = "$(git -C "sub proj.git" rev-parse --verify refs/heads/from-mainline)"
	)
'

test_expect_success 'push sub dir/ with --rejoin and --message' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		git subtree push --prefix="sub dir" --message="Split & rejoin" --annotate="*" --rejoin ./"sub proj" from-mainline &&
		test "$(last_commit_subject)" = "Split & rejoin" &&
		split_hash="$(git rev-parse --verify HEAD^2)" &&
		test "$split_hash" = "$(git -C "sub proj" rev-parse --verify refs/heads/from-mainline)"
	)
'

test_expect_success 'push "sub dir"/ with --rejoin and --squash' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" --squash FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git subtree pull --prefix="sub dir" --squash ./"sub proj" HEAD &&
		MAIN=$(git rev-parse --verify HEAD) &&
		SUB=$(git -C "sub proj" rev-parse --verify HEAD) &&

		SPLIT=$(git subtree split --prefix="sub dir" --annotate="*") &&
		git subtree push --prefix="sub dir" --annotate="*" --rejoin --squash ./"sub proj" from-mainline &&

		test_must_fail git merge-base --is-ancestor $SUB HEAD &&
		test_must_fail git merge-base --is-ancestor $SPLIT HEAD &&
		git rev-list HEAD ^$MAIN >commit-list &&
		test_line_count = 2 commit-list &&
		test "$(git rev-parse --verify HEAD:)"           = "$(git rev-parse --verify $MAIN:)" &&
		test "$(git rev-parse --verify HEAD:"sub dir")"  = "$(git rev-parse --verify $SPLIT:)" &&
		test "$(git rev-parse --verify HEAD^1)"          = $MAIN &&
		test "$(git rev-parse --verify HEAD^2)"         != $SPLIT &&
		test "$(git rev-parse --verify HEAD^2:)"         = "$(git rev-parse --verify $SPLIT:)" &&
		test "$(last_commit_subject)" = "Split '\''sub dir/'\'' into commit '\''$SPLIT'\''" &&
		test "$SPLIT" = "$(git -C "sub proj" rev-parse --verify refs/heads/from-mainline)"
	)
'

test_expect_success 'push "sub dir"/ with --branch' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(git subtree split --prefix="sub dir" --annotate="*") &&
		git subtree push --prefix="sub dir" --annotate="*" --branch subproj-br ./"sub proj" from-mainline &&
		test "$(git rev-parse subproj-br)" = "$split_hash" &&
		test "$split_hash" = "$(git -C "sub proj" rev-parse --verify refs/heads/from-mainline)"
	)
'

test_expect_success 'check hash of push' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(git subtree split --prefix="sub dir" --annotate="*") &&
		git subtree push --prefix="sub dir" --annotate="*" --branch subproj-br ./"sub proj" from-mainline &&
		test "$(git rev-parse subproj-br)" = "$split_hash" &&
		# Check hash of split
		new_hash=$(git rev-parse subproj-br^2) &&
		(
			cd ./"sub proj" &&
			subdir_hash=$(git rev-parse HEAD) &&
			test "$new_hash" = "$subdir_hash"
		) &&
		test "$split_hash" = "$(git -C "sub proj" rev-parse --verify refs/heads/from-mainline)"
	)
'

test_expect_success 'push "sub dir"/ with --branch for an existing branch' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git branch subproj-br FETCH_HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(git subtree split --prefix="sub dir" --annotate="*") &&
		git subtree push --prefix="sub dir" --annotate="*" --branch subproj-br ./"sub proj" from-mainline &&
		test "$(git rev-parse subproj-br)" = "$split_hash" &&
		test "$split_hash" = "$(git -C "sub proj" rev-parse --verify refs/heads/from-mainline)"
	)
'

test_expect_success 'push "sub dir"/ with --branch for an incompatible branch' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git branch init HEAD &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		test_must_fail git subtree push --prefix="sub dir" --branch init "./sub proj" from-mainline
	)
'

test_expect_success 'push "sub dir"/ with a local rev' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		bad_tree=$(git rev-parse --verify HEAD:"sub dir") &&
		good_tree=$(git rev-parse --verify HEAD^:"sub dir") &&
		git subtree push --prefix="sub dir" --annotate="*" ./"sub proj" HEAD^:from-mainline &&
		split_tree=$(git -C "sub proj" rev-parse --verify refs/heads/from-mainline:) &&
		test "$split_tree" = "$good_tree"
	)
'

test_expect_success 'push after annotated tag was added/merged with --squash pre-v2.32.0' '
	test_create_pre2_32_repo "$test_count" &&
	test_create_commit "$test_count-clone" sub/main-sub1 &&
	git -C "$test_count-clone" subtree push --prefix="sub" "../$test_count-sub" from-mainline
'

#
# Validity checking
#

test_expect_success 'make sure exactly the right set of files ends up in the subproj' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$test_count/sub proj" sub3 &&
	test_create_commit "$test_count" "sub dir"/main-sub3 &&
	(
		cd "$test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	test_create_commit "$test_count/sub proj" sub4 &&
	(
		cd "$test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub4 &&
	(
		cd "$test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD &&

		test_write_lines main-sub1 main-sub2 main-sub3 main-sub4 \
			sub1 sub2 sub3 sub4 >expect &&
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'make sure the subproj *only* contains commits that affect the "sub dir"' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$test_count/sub proj" sub3 &&
	test_create_commit "$test_count" "sub dir"/main-sub3 &&
	(
		cd "$test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	test_create_commit "$test_count/sub proj" sub4 &&
	(
		cd "$test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub4 &&
	(
		cd "$test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD &&

		test_write_lines main-sub1 main-sub2 main-sub3 main-sub4 \
			sub1 sub2 sub3 sub4 >expect &&
		git log --name-only --pretty=format:"" >log &&
		sort <log | sed "/^\$/ d" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'make sure exactly the right set of files ends up in the mainline' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$test_count/sub proj" sub3 &&
	test_create_commit "$test_count" "sub dir"/main-sub3 &&
	(
		cd "$test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	test_create_commit "$test_count/sub proj" sub4 &&
	(
		cd "$test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub4 &&
	(
		cd "$test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	(
		cd "$test_count" &&
		git subtree pull --prefix="sub dir" ./"sub proj" HEAD &&

		test_write_lines main1 main2 >chkm &&
		test_write_lines main-sub1 main-sub2 main-sub3 main-sub4 >chkms &&
		sed "s,^,sub dir/," chkms >chkms_sub &&
		test_write_lines sub1 sub2 sub3 sub4 >chks &&
		sed "s,^,sub dir/," chks >chks_sub &&

		cat chkm chkms_sub chks_sub >expect &&
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'make sure each filename changed exactly once in the entire history' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git config log.date relative &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$test_count/sub proj" sub3 &&
	test_create_commit "$test_count" "sub dir"/main-sub3 &&
	(
		cd "$test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	test_create_commit "$test_count/sub proj" sub4 &&
	(
		cd "$test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub4 &&
	(
		cd "$test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	(
		cd "$test_count" &&
		git subtree pull --prefix="sub dir" ./"sub proj" HEAD &&

		test_write_lines main1 main2 >chkm &&
		test_write_lines sub1 sub2 sub3 sub4 >chks &&
		test_write_lines main-sub1 main-sub2 main-sub3 main-sub4 >chkms &&
		sed "s,^,sub dir/," chkms >chkms_sub &&

		# main-sub?? and /"sub dir"/main-sub?? both change, because those are the
		# changes that were split into their own history.  And "sub dir"/sub?? never
		# change, since they were *only* changed in the subtree branch.
		git log --name-only --pretty=format:"" >log &&
		sort <log >sorted-log &&
		sed "/^$/ d" sorted-log >actual &&

		cat chkms chkm chks chkms_sub >expect-unsorted &&
		sort expect-unsorted >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'make sure the --rejoin commits never make it into subproj' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$test_count/sub proj" sub3 &&
	test_create_commit "$test_count" "sub dir"/main-sub3 &&
	(
		cd "$test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	test_create_commit "$test_count/sub proj" sub4 &&
	(
		cd "$test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub4 &&
	(
		cd "$test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	(
		cd "$test_count" &&
		git subtree pull --prefix="sub dir" ./"sub proj" HEAD &&
		test "$(git log --pretty=format:"%s" HEAD^2 | grep -i split)" = ""
	)
'

test_expect_success 'make sure no "git subtree" tagged commits make it into subproj' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$test_count/sub proj" sub3 &&
	test_create_commit "$test_count" "sub dir"/main-sub3 &&
	(
		cd "$test_count/sub proj" &&
		git fetch .. subproj-br &&
		 git merge FETCH_HEAD
	) &&
	test_create_commit "$test_count/sub proj" sub4 &&
	(
		cd "$test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub4 &&
	(
		cd "$test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	(
		cd "$test_count" &&
		git subtree pull --prefix="sub dir" ./"sub proj" HEAD &&

		# They are meaningless to subproj since one side of the merge refers to the mainline
		test "$(git log --pretty=format:"%s%n%b" HEAD^2 | grep "git-subtree.*:")" = ""
	)
'

#
# A new set of tests
#

test_expect_success 'make sure "git subtree split" find the correct parent' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git branch subproj-ref FETCH_HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	(
		cd "$test_count" &&
		git subtree split --prefix="sub dir" --branch subproj-br &&

		# at this point, the new commit parent should be subproj-ref, if it is
		# not, something went wrong (the "newparent" of "HEAD~" commit should
		# have been sub2, but it was not, because its cache was not set to
		# itself)
		test "$(git log --pretty=format:%P -1 subproj-br)" = "$(git rev-parse subproj-ref)"
	)
'

test_expect_success 'split a new subtree without --onto option' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	(
		cd "$test_count" &&
		git subtree split --prefix="sub dir" --branch subproj-br
	) &&
	mkdir "$test_count"/"sub dir2" &&
	test_create_commit "$test_count" "sub dir2"/main-sub2 &&
	(
		cd "$test_count" &&

		# also test that we still can split out an entirely new subtree
		# if the parent of the first commit in the tree is not empty,
		# then the new subtree has accidentally been attached to something
		git subtree split --prefix="sub dir2" --branch subproj2-br &&
		test "$(git log --pretty=format:%P -1 subproj2-br)" = ""
	)
'

test_expect_success 'verify one file change per commit' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git branch sub1 FETCH_HEAD &&
		git subtree add --prefix="sub dir" sub1
	) &&
	test_create_commit "$test_count/sub proj" sub2 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	(
		cd "$test_count" &&
		git subtree split --prefix="sub dir" --branch subproj-br
	) &&
	mkdir "$test_count"/"sub dir2" &&
	test_create_commit "$test_count" "sub dir2"/main-sub2 &&
	(
		cd "$test_count" &&
		git subtree split --prefix="sub dir2" --branch subproj2-br &&

		git log --format="%H" >commit-list &&
		while read commit
		do
			git log -n1 --format="" --name-only "$commit" >file-list &&
			test_line_count -le 1 file-list || return 1
		done <commit-list
	)
'

test_expect_success 'push split to subproj' '
	subtree_test_create_repo "$test_count" &&
	subtree_test_create_repo "$test_count/sub proj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/sub proj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./"sub proj" HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub1 &&
	test_create_commit "$test_count" main2 &&
	test_create_commit "$test_count/sub proj" sub2 &&
	test_create_commit "$test_count" "sub dir"/main-sub2 &&
	(
		cd $test_count/"sub proj" &&
		git branch sub-branch-1 &&
		cd .. &&
		git fetch ./"sub proj" HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$test_count" "sub dir"/main-sub3 &&
	(
		cd "$test_count" &&
		git subtree push ./"sub proj" --prefix "sub dir" sub-branch-1 &&
		cd ./"sub proj" &&
		git checkout sub-branch-1 &&
		test "$(last_commit_subject)" = "sub dir/main-sub3"
	)
'

#
# This test covers 2 cases in subtree split copy_or_skip code
# 1) Merges where one parent is a superset of the changes of the other
#    parent regarding changes to the subtree, in this case the merge
#    commit should be copied
# 2) Merges where only one parent operate on the subtree, and the merge
#    commit should be skipped
#
# (1) is checked by ensuring subtree_tip is a descendent of subtree_branch
# (2) should have a check added (not_a_subtree_change shouldn't be present
#     on the produced subtree)
#
# Other related cases which are not tested (or currently handled correctly)
# - Case (1) where there are more than 2 parents, it will sometimes correctly copy
#   the merge, and sometimes not
# - Merge commit where both parents have same tree as the merge, currently
#   will always be skipped, even if they reached that state via different
#   set of commits.
#

test_expect_success 'subtree descendant check' '
	subtree_test_create_repo "$test_count" &&
	defaultBranch=$(sed "s,ref: refs/heads/,," "$test_count/.git/HEAD") &&
	test_create_commit "$test_count" folder_subtree/a &&
	(
		cd "$test_count" &&
		git branch branch
	) &&
	test_create_commit "$test_count" folder_subtree/0 &&
	test_create_commit "$test_count" folder_subtree/b &&
	cherry=$(cd "$test_count" && git rev-parse HEAD) &&
	(
		cd "$test_count" &&
		git checkout branch
	) &&
	test_create_commit "$test_count" commit_on_branch &&
	(
		cd "$test_count" &&
		git cherry-pick $cherry &&
		git checkout $defaultBranch &&
		git merge -m "merge should be kept on subtree" branch &&
		git branch no_subtree_work_branch
	) &&
	test_create_commit "$test_count" folder_subtree/d &&
	(
		cd "$test_count" &&
		git checkout no_subtree_work_branch
	) &&
	test_create_commit "$test_count" not_a_subtree_change &&
	(
		cd "$test_count" &&
		git checkout $defaultBranch &&
		git merge -m "merge should be skipped on subtree" no_subtree_work_branch &&

		git subtree split --prefix folder_subtree/ --branch subtree_tip $defaultBranch &&
		git subtree split --prefix folder_subtree/ --branch subtree_branch branch &&
		test $(git rev-list --count subtree_tip..subtree_branch) = 0
	)
'

test_done
