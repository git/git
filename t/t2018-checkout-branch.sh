#!/bin/sh

test_description='checkout '

. ./test-lib.sh

# Arguments: <branch> <sha> [<checkout options>]
#
# Runs "git checkout" to switch to <branch>, testing that
#
#   1) we are on the specified branch, <branch>;
#   2) HEAD is <sha>; if <sha> is not specified, the old HEAD is used.
#
# If <checkout options> is not specified, "git checkout" is run with -b.
do_checkout() {
	exp_branch=$1 &&
	exp_ref="refs/heads/$exp_branch" &&

	# if <sha> is not specified, use HEAD.
	exp_sha=${2:-$(git rev-parse --verify HEAD)} &&

	# default options for git checkout: -b
	if [ -z "$3" ]; then
		opts="-b"
	else
		opts="$3"
	fi

	git checkout $opts $exp_branch $exp_sha &&

	test $exp_ref = $(git rev-parse --symbolic-full-name HEAD) &&
	test $exp_sha = $(git rev-parse --verify HEAD)
}

test_dirty_unmergeable() {
	! git diff --exit-code >/dev/null
}

setup_dirty_unmergeable() {
	echo >>file1 change2
}

test_dirty_mergeable() {
	! git diff --cached --exit-code >/dev/null
}

setup_dirty_mergeable() {
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

test_expect_success 'checkout -b to a new branch, set to HEAD' '
	do_checkout branch2
'

test_expect_success 'checkout -b to a new branch, set to an explicit ref' '
	git checkout branch1 &&
	git branch -D branch2 &&

	do_checkout branch2 $HEAD1
'

test_expect_success 'checkout -b to a new branch with unmergeable changes fails' '
	git checkout branch1 &&

	# clean up from previous test
	git branch -D branch2 &&

	setup_dirty_unmergeable &&
	test_must_fail do_checkout branch2 $HEAD1 &&
	test_dirty_unmergeable
'

test_expect_success 'checkout -f -b to a new branch with unmergeable changes discards changes' '
	# still dirty and on branch1
	do_checkout branch2 $HEAD1 "-f -b" &&
	test_must_fail test_dirty_unmergeable
'

test_expect_success 'checkout -b to a new branch preserves mergeable changes' '
	git checkout branch1 &&

	# clean up from previous test
	git branch -D branch2 &&

	setup_dirty_mergeable &&
	do_checkout branch2 $HEAD1 &&
	test_dirty_mergeable
'

test_expect_success 'checkout -f -b to a new branch with mergeable changes discards changes' '
	# clean up from previous test
	git reset --hard &&

	git checkout branch1 &&

	# clean up from previous test
	git branch -D branch2 &&

	setup_dirty_mergeable &&
	do_checkout branch2 $HEAD1 "-f -b" &&
	test_must_fail test_dirty_mergeable
'

test_expect_success 'checkout -b to an existing branch fails' '
	git reset --hard HEAD &&

	test_must_fail do_checkout branch2 $HEAD2
'

test_expect_success 'checkout -b to @{-1} fails with the right branch name' '
	git reset --hard HEAD &&
	git checkout branch1 &&
	git checkout branch2 &&
	echo  >expect "fatal: A branch named '\''branch1'\'' already exists." &&
	test_must_fail git checkout -b @{-1} 2>actual &&
	test_i18ncmp expect actual
'

test_expect_success 'checkout -B to an existing branch resets branch to HEAD' '
	git checkout branch1 &&

	do_checkout branch2 "" -B
'

test_expect_success 'checkout -B to an existing branch from detached HEAD resets branch to HEAD' '
	git checkout $(git rev-parse --verify HEAD) &&

	do_checkout branch2 "" -B
'

test_expect_success 'checkout -B to an existing branch with an explicit ref resets branch to that ref' '
	git checkout branch1 &&

	do_checkout branch2 $HEAD1 -B
'

test_expect_success 'checkout -B to an existing branch with unmergeable changes fails' '
	git checkout branch1 &&

	setup_dirty_unmergeable &&
	test_must_fail do_checkout branch2 $HEAD1 -B &&
	test_dirty_unmergeable
'

test_expect_success 'checkout -f -B to an existing branch with unmergeable changes discards changes' '
	# still dirty and on branch1
	do_checkout branch2 $HEAD1 "-f -B" &&
	test_must_fail test_dirty_unmergeable
'

test_expect_success 'checkout -B to an existing branch preserves mergeable changes' '
	git checkout branch1 &&

	setup_dirty_mergeable &&
	do_checkout branch2 $HEAD1 -B &&
	test_dirty_mergeable
'

test_expect_success 'checkout -f -B to an existing branch with mergeable changes discards changes' '
	# clean up from previous test
	git reset --hard &&

	git checkout branch1 &&

	setup_dirty_mergeable &&
	do_checkout branch2 $HEAD1 "-f -B" &&
	test_must_fail test_dirty_mergeable
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

test_done
