#!/bin/sh
#
# Copyright (c) 2006 Josh England
#

test_description='Test the post-checkout hook.'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	test_hook --setup post-checkout <<-\EOF &&
	echo "$@" >.git/post-checkout.args
	EOF
	test_commit one &&
	test_commit two &&
	test_commit rebase-on-me &&
	git reset --hard HEAD^ &&
	test_commit three
'

test_expect_success 'post-checkout receives the right arguments with HEAD unchanged ' '
	test_when_finished "rm -f .git/post-checkout.args" &&
	git checkout main &&
	read old new flag <.git/post-checkout.args &&
	test $old = $new && test $flag = 1
'

test_expect_success 'post-checkout args are correct with git checkout -b ' '
	test_when_finished "rm -f .git/post-checkout.args" &&
	git checkout -b new1 &&
	read old new flag <.git/post-checkout.args &&
	test $old = $new && test $flag = 1
'

test_expect_success 'post-checkout receives the right args with HEAD changed ' '
	test_when_finished "rm -f .git/post-checkout.args" &&
	git checkout two &&
	read old new flag <.git/post-checkout.args &&
	test $old != $new && test $flag = 1
'

test_expect_success 'post-checkout receives the right args when not switching branches ' '
	test_when_finished "rm -f .git/post-checkout.args" &&
	git checkout main -- three.t &&
	read old new flag <.git/post-checkout.args &&
	test $old = $new && test $flag = 0
'

test_rebase () {
	args="$*" &&
	test_expect_success "post-checkout is triggered on rebase $args" '
		test_when_finished "rm -f .git/post-checkout.args" &&
		git checkout -B rebase-test main &&
		rm -f .git/post-checkout.args &&
		git rebase $args rebase-on-me &&
		read old new flag <.git/post-checkout.args &&
		test_cmp_rev main $old &&
		test_cmp_rev rebase-on-me $new &&
		test $flag = 1
	'

	test_expect_success "post-checkout is triggered on rebase $args with fast-forward" '
		test_when_finished "rm -f .git/post-checkout.args" &&
		git checkout -B ff-rebase-test rebase-on-me^ &&
		rm -f .git/post-checkout.args &&
		git rebase $args rebase-on-me &&
		read old new flag <.git/post-checkout.args &&
		test_cmp_rev rebase-on-me^ $old &&
		test_cmp_rev rebase-on-me $new &&
		test $flag = 1
	'

	test_expect_success "rebase $args fast-forward branch checkout runs post-checkout hook" '
		test_when_finished "test_might_fail git rebase --abort" &&
		test_when_finished "rm -f .git/post-checkout.args" &&
		git update-ref refs/heads/rebase-fast-forward three &&
		git checkout two  &&
		rm -f .git/post-checkout.args &&
		git rebase $args HEAD rebase-fast-forward  &&
		read old new flag <.git/post-checkout.args &&
		test_cmp_rev two $old &&
		test_cmp_rev three $new &&
		test $flag = 1
	'

	test_expect_success "rebase $args checkout does not remove untracked files" '
		test_when_finished "test_might_fail git rebase --abort" &&
		test_when_finished "rm -f .git/post-checkout.args" &&
		git update-ref refs/heads/rebase-fast-forward three &&
		git checkout two &&
		rm -f .git/post-checkout.args &&
		echo untracked >three.t &&
		test_when_finished "rm three.t" &&
		test_must_fail git rebase $args HEAD rebase-fast-forward 2>err &&
		grep "untracked working tree files would be overwritten by checkout" err &&
		test_path_is_missing .git/post-checkout.args

'
}

test_rebase --apply &&
test_rebase --merge

test_expect_success 'post-checkout hook is triggered by clone' '
	mkdir -p templates/hooks &&
	write_script templates/hooks/post-checkout <<-\EOF &&
	echo "$@" >"$GIT_DIR/post-checkout.args"
	EOF
	git clone --template=templates . clone3 &&
	test -f clone3/.git/post-checkout.args
'

test_done
