#!/bin/sh

test_description='checkout'

TEST_CREATE_REPO_NO_TEMPLATE=1
TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# Arguments: [!] <branch> <oid> [<checkout options>]
#
# Runs "git checkout" to switch to <branch>, testing that
#
#   1) we are on the specified branch, <branch>;
#   2) HEAD is <oid>; if <oid> is not specified, the old HEAD is used.
#
# If <checkout options> is not specified, "git checkout" is run with -b.
#
# If the first argument is `!`, "git checkout" is expected to fail when
# it is run.
do_checkout () {
	should_fail= &&
	if test "x$1" = "x!"
	then
		should_fail=yes &&
		shift
	fi &&
	exp_branch=$1 &&
	exp_ref="refs/heads/$exp_branch" &&

	# if <oid> is not specified, use HEAD.
	exp_oid=${2:-$(git rev-parse --verify HEAD)} &&

	# default options for git checkout: -b
	if test -z "$3"
	then
		opts="-b"
	else
		opts="$3"
	fi

	if test -n "$should_fail"
	then
		test_must_fail git checkout $opts $exp_branch $exp_oid
	else
		git checkout $opts $exp_branch $exp_oid &&
		echo "$exp_ref" >ref.expect &&
		git rev-parse --symbolic-full-name HEAD >ref.actual &&
		test_cmp ref.expect ref.actual &&
		echo "$exp_oid" >oid.expect &&
		git rev-parse --verify HEAD >oid.actual &&
		test_cmp oid.expect oid.actual
	fi
}

test_dirty_unmergeable () {
	test_expect_code 1 git diff --exit-code
}

test_dirty_unmergeable_discards_changes () {
	git diff --exit-code
}

setup_dirty_unmergeable () {
	echo >>file1 change2
}

test_dirty_mergeable () {
	test_expect_code 1 git diff --cached --exit-code
}

test_dirty_mergeable_discards_changes () {
	git diff --cached --exit-code
}

setup_dirty_mergeable () {
	echo >file2 file2 &&
	git add file2
}

test_expect_success 'setup' '
	test_commit initial file1 &&
	HEAD1=$(git rev-parse --verify HEAD) &&

	test_commit change1 file1 &&
	HEAD2=$(git rev-parse --verify HEAD) &&

	git branch -m branch1
'

test_expect_success 'checkout a branch without refs/heads/* prefix' '
	git clone --no-tags . repo-odd-prefix &&
	(
		cd repo-odd-prefix &&

		origin=$(git symbolic-ref refs/remotes/origin/HEAD) &&
		git symbolic-ref refs/heads/a-branch "$origin" &&

		git checkout -f a-branch &&
		git checkout -f a-branch
	)
'

test_expect_success 'checkout -b to a new branch, set to HEAD' '
	test_when_finished "
		git checkout branch1 &&
		test_might_fail git branch -D branch2" &&
	do_checkout branch2
'

test_expect_success 'checkout -b to a merge base' '
	test_when_finished "
		git checkout branch1 &&
		test_might_fail git branch -D branch2" &&
	git checkout -b branch2 branch1...
'

test_expect_success 'checkout -b to a new branch, set to an explicit ref' '
	test_when_finished "
		git checkout branch1 &&
		test_might_fail git branch -D branch2" &&
	do_checkout branch2 $HEAD1
'

test_expect_success 'checkout -b to a new branch with unmergeable changes fails' '
	setup_dirty_unmergeable &&
	do_checkout ! branch2 $HEAD1 &&
	test_dirty_unmergeable
'

test_expect_success 'checkout -f -b to a new branch with unmergeable changes discards changes' '
	test_when_finished "
		git checkout branch1 &&
		test_might_fail git branch -D branch2" &&

	# still dirty and on branch1
	do_checkout branch2 $HEAD1 "-f -b" &&
	test_dirty_unmergeable_discards_changes
'

test_expect_success 'checkout -b to a new branch preserves mergeable changes' '
	test_when_finished "
		git reset --hard &&
		git checkout branch1 &&
		test_might_fail git branch -D branch2" &&

	setup_dirty_mergeable &&
	do_checkout branch2 $HEAD1 &&
	test_dirty_mergeable
'

test_expect_success 'checkout -f -b to a new branch with mergeable changes discards changes' '
	test_when_finished git reset --hard HEAD &&
	setup_dirty_mergeable &&
	do_checkout branch2 $HEAD1 "-f -b" &&
	test_dirty_mergeable_discards_changes
'

test_expect_success 'checkout -b to an existing branch fails' '
	test_when_finished git reset --hard HEAD &&
	do_checkout ! branch2 $HEAD2
'

test_expect_success 'checkout -b to @{-1} fails with the right branch name' '
	git checkout branch1 &&
	git checkout branch2 &&
	echo  >expect "fatal: a branch named '\''branch1'\'' already exists" &&
	test_must_fail git checkout -b @{-1} 2>actual &&
	test_cmp expect actual
'

test_expect_success 'checkout -B to an existing branch resets branch to HEAD' '
	git checkout branch1 &&

	do_checkout branch2 "" -B
'

test_expect_success 'checkout -B to a merge base' '
	git checkout branch1 &&

	git checkout -B branch2 branch1...
'

test_expect_success 'checkout -B to an existing branch from detached HEAD resets branch to HEAD' '
	head=$(git rev-parse --verify HEAD) &&
	git checkout "$head" &&

	do_checkout branch2 "" -B
'

test_expect_success 'checkout -B to an existing branch with an explicit ref resets branch to that ref' '
	git checkout branch1 &&

	do_checkout branch2 $HEAD1 -B
'

test_expect_success 'checkout -B to an existing branch with unmergeable changes fails' '
	git checkout branch1 &&

	setup_dirty_unmergeable &&
	do_checkout ! branch2 $HEAD1 -B &&
	test_dirty_unmergeable
'

test_expect_success 'checkout -f -B to an existing branch with unmergeable changes discards changes' '
	# still dirty and on branch1
	do_checkout branch2 $HEAD1 "-f -B" &&
	test_dirty_unmergeable_discards_changes
'

test_expect_success 'checkout -B to an existing branch preserves mergeable changes' '
	test_when_finished git reset --hard &&
	git checkout branch1 &&

	setup_dirty_mergeable &&
	do_checkout branch2 $HEAD1 -B &&
	test_dirty_mergeable
'

test_expect_success 'checkout -f -B to an existing branch with mergeable changes discards changes' '
	git checkout branch1 &&

	setup_dirty_mergeable &&
	do_checkout branch2 $HEAD1 "-f -B" &&
	test_dirty_mergeable_discards_changes
'

test_expect_success 'checkout -b <describe>' '
	git tag -f -m "First commit" initial initial &&
	git checkout -f change1 &&
	name=$(git describe) &&
	git checkout -b $name &&
	git diff --exit-code change1 &&
	echo "refs/heads/$name" >expect &&
	git symbolic-ref HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'checkout -B to the current branch works' '
	git checkout branch1 &&
	git checkout -B branch1-scratch &&

	setup_dirty_mergeable &&
	git checkout -B branch1-scratch initial &&
	test_dirty_mergeable
'

test_expect_success 'checkout -b after clone --no-checkout does a checkout of HEAD' '
	git init src &&
	test_commit -C src a &&
	rev="$(git -C src rev-parse HEAD)" &&
	git clone --no-checkout src dest &&
	git -C dest checkout "$rev" -b branch &&
	test_path_is_file dest/a.t
'

test_expect_success 'checkout -b to a new branch preserves mergeable changes despite sparse-checkout' '
	test_when_finished "
		git reset --hard &&
		git checkout branch1-scratch &&
		test_might_fail git branch -D branch3 &&
		git config core.sparseCheckout false &&
		rm -rf .git/info" &&

	test_commit file2 &&

	echo stuff >>file1 &&
	mkdir .git/info &&
	echo file2 >.git/info/sparse-checkout &&
	git config core.sparseCheckout true &&

	CURHEAD=$(git rev-parse HEAD) &&
	do_checkout branch3 $CURHEAD &&

	echo file1 >expect &&
	git diff --name-only >actual &&
	test_cmp expect actual
'

test_expect_success 'checkout -b rejects an invalid start point' '
	test_must_fail git checkout -b branch4 file1 2>err &&
	test_grep "is not a commit" err
'

test_expect_success 'checkout -b rejects an extra path argument' '
	test_must_fail git checkout -b branch5 branch1 file1 2>err &&
	test_grep "Cannot update paths and switch to branch" err
'

test_done
