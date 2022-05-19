#!/bin/sh

test_description='checkout'

. ./test-lib.sh

# Arguments: [!] <branch> <oid> [<checkout options>]
#
# Runs "but checkout" to switch to <branch>, testing that
#
#   1) we are on the specified branch, <branch>;
#   2) HEAD is <oid>; if <oid> is not specified, the old HEAD is used.
#
# If <checkout options> is not specified, "but checkout" is run with -b.
#
# If the first argument is `!`, "but checkout" is expected to fail when
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
	exp_oid=${2:-$(but rev-parse --verify HEAD)} &&

	# default options for but checkout: -b
	if test -z "$3"
	then
		opts="-b"
	else
		opts="$3"
	fi

	if test -n "$should_fail"
	then
		test_must_fail but checkout $opts $exp_branch $exp_oid
	else
		but checkout $opts $exp_branch $exp_oid &&
		echo "$exp_ref" >ref.expect &&
		but rev-parse --symbolic-full-name HEAD >ref.actual &&
		test_cmp ref.expect ref.actual &&
		echo "$exp_oid" >oid.expect &&
		but rev-parse --verify HEAD >oid.actual &&
		test_cmp oid.expect oid.actual
	fi
}

test_dirty_unmergeable () {
	test_expect_code 1 but diff --exit-code
}

test_dirty_unmergeable_discards_changes () {
	but diff --exit-code
}

setup_dirty_unmergeable () {
	echo >>file1 change2
}

test_dirty_mergeable () {
	test_expect_code 1 but diff --cached --exit-code
}

test_dirty_mergeable_discards_changes () {
	but diff --cached --exit-code
}

setup_dirty_mergeable () {
	echo >file2 file2 &&
	but add file2
}

test_expect_success 'setup' '
	test_cummit initial file1 &&
	HEAD1=$(but rev-parse --verify HEAD) &&

	test_cummit change1 file1 &&
	HEAD2=$(but rev-parse --verify HEAD) &&

	but branch -m branch1
'

test_expect_success 'checkout a branch without refs/heads/* prefix' '
	but clone --no-tags . repo-odd-prefix &&
	(
		cd repo-odd-prefix &&

		origin=$(but symbolic-ref refs/remotes/origin/HEAD) &&
		but symbolic-ref refs/heads/a-branch "$origin" &&

		but checkout -f a-branch &&
		but checkout -f a-branch
	)
'

test_expect_success 'checkout -b to a new branch, set to HEAD' '
	test_when_finished "
		but checkout branch1 &&
		test_might_fail but branch -D branch2" &&
	do_checkout branch2
'

test_expect_success 'checkout -b to a merge base' '
	test_when_finished "
		but checkout branch1 &&
		test_might_fail but branch -D branch2" &&
	but checkout -b branch2 branch1...
'

test_expect_success 'checkout -b to a new branch, set to an explicit ref' '
	test_when_finished "
		but checkout branch1 &&
		test_might_fail but branch -D branch2" &&
	do_checkout branch2 $HEAD1
'

test_expect_success 'checkout -b to a new branch with unmergeable changes fails' '
	setup_dirty_unmergeable &&
	do_checkout ! branch2 $HEAD1 &&
	test_dirty_unmergeable
'

test_expect_success 'checkout -f -b to a new branch with unmergeable changes discards changes' '
	test_when_finished "
		but checkout branch1 &&
		test_might_fail but branch -D branch2" &&

	# still dirty and on branch1
	do_checkout branch2 $HEAD1 "-f -b" &&
	test_dirty_unmergeable_discards_changes
'

test_expect_success 'checkout -b to a new branch preserves mergeable changes' '
	test_when_finished "
		but reset --hard &&
		but checkout branch1 &&
		test_might_fail but branch -D branch2" &&

	setup_dirty_mergeable &&
	do_checkout branch2 $HEAD1 &&
	test_dirty_mergeable
'

test_expect_success 'checkout -f -b to a new branch with mergeable changes discards changes' '
	test_when_finished but reset --hard HEAD &&
	setup_dirty_mergeable &&
	do_checkout branch2 $HEAD1 "-f -b" &&
	test_dirty_mergeable_discards_changes
'

test_expect_success 'checkout -b to an existing branch fails' '
	test_when_finished but reset --hard HEAD &&
	do_checkout ! branch2 $HEAD2
'

test_expect_success 'checkout -b to @{-1} fails with the right branch name' '
	but checkout branch1 &&
	but checkout branch2 &&
	echo  >expect "fatal: a branch named '\''branch1'\'' already exists" &&
	test_must_fail but checkout -b @{-1} 2>actual &&
	test_cmp expect actual
'

test_expect_success 'checkout -B to an existing branch resets branch to HEAD' '
	but checkout branch1 &&

	do_checkout branch2 "" -B
'

test_expect_success 'checkout -B to a merge base' '
	but checkout branch1 &&

	but checkout -B branch2 branch1...
'

test_expect_success 'checkout -B to an existing branch from detached HEAD resets branch to HEAD' '
	head=$(but rev-parse --verify HEAD) &&
	but checkout "$head" &&

	do_checkout branch2 "" -B
'

test_expect_success 'checkout -B to an existing branch with an explicit ref resets branch to that ref' '
	but checkout branch1 &&

	do_checkout branch2 $HEAD1 -B
'

test_expect_success 'checkout -B to an existing branch with unmergeable changes fails' '
	but checkout branch1 &&

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
	test_when_finished but reset --hard &&
	but checkout branch1 &&

	setup_dirty_mergeable &&
	do_checkout branch2 $HEAD1 -B &&
	test_dirty_mergeable
'

test_expect_success 'checkout -f -B to an existing branch with mergeable changes discards changes' '
	but checkout branch1 &&

	setup_dirty_mergeable &&
	do_checkout branch2 $HEAD1 "-f -B" &&
	test_dirty_mergeable_discards_changes
'

test_expect_success 'checkout -b <describe>' '
	but tag -f -m "First cummit" initial initial &&
	but checkout -f change1 &&
	name=$(but describe) &&
	but checkout -b $name &&
	but diff --exit-code change1 &&
	echo "refs/heads/$name" >expect &&
	but symbolic-ref HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'checkout -B to the current branch works' '
	but checkout branch1 &&
	but checkout -B branch1-scratch &&

	setup_dirty_mergeable &&
	but checkout -B branch1-scratch initial &&
	test_dirty_mergeable
'

test_expect_success 'checkout -b after clone --no-checkout does a checkout of HEAD' '
	but init src &&
	test_cummit -C src a &&
	rev="$(but -C src rev-parse HEAD)" &&
	but clone --no-checkout src dest &&
	but -C dest checkout "$rev" -b branch &&
	test_path_is_file dest/a.t
'

test_expect_success 'checkout -b to a new branch preserves mergeable changes despite sparse-checkout' '
	test_when_finished "
		but reset --hard &&
		but checkout branch1-scratch &&
		test_might_fail but branch -D branch3 &&
		but config core.sparseCheckout false &&
		rm .but/info/sparse-checkout" &&

	test_cummit file2 &&

	echo stuff >>file1 &&
	echo file2 >.but/info/sparse-checkout &&
	but config core.sparseCheckout true &&

	CURHEAD=$(but rev-parse HEAD) &&
	do_checkout branch3 $CURHEAD &&

	echo file1 >expect &&
	but diff --name-only >actual &&
	test_cmp expect actual
'

test_expect_success 'checkout -b rejects an invalid start point' '
	test_must_fail but checkout -b branch4 file1 2>err &&
	test_i18ngrep "is not a cummit" err
'

test_expect_success 'checkout -b rejects an extra path argument' '
	test_must_fail but checkout -b branch5 branch1 file1 2>err &&
	test_i18ngrep "Cannot update paths and switch to branch" err
'

test_done
