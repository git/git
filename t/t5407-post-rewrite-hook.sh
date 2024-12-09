#!/bin/sh
#
# Copyright (c) 2010 Thomas Rast
#

test_description='Test the post-rewrite hook.'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit A foo A &&
	test_commit B foo B &&
	test_commit C foo C &&
	test_commit D foo D &&
	git checkout A^0 &&
	test_commit E bar E &&
	test_commit F foo F &&
	git checkout B &&
	git merge E &&
	git tag merge-E &&
	test_commit G G &&
	test_commit H H &&
	test_commit I I &&
	git checkout main &&

	test_hook --setup post-rewrite <<-EOF
	echo \$@ > "$TRASH_DIRECTORY"/post-rewrite.args
	cat > "$TRASH_DIRECTORY"/post-rewrite.data
	EOF
'

clear_hook_input () {
	rm -f post-rewrite.args post-rewrite.data
}

verify_hook_input () {
	test_cmp expected.args "$TRASH_DIRECTORY"/post-rewrite.args &&
	test_cmp expected.data "$TRASH_DIRECTORY"/post-rewrite.data
}

test_expect_success 'git commit --amend' '
	clear_hook_input &&
	echo "D new message" > newmsg &&
	oldsha=$(git rev-parse HEAD^0) &&
	git commit -Fnewmsg --amend &&
	echo amend > expected.args &&
	echo $oldsha $(git rev-parse HEAD^0) > expected.data &&
	verify_hook_input
'

test_expect_success 'git commit --amend --no-post-rewrite' '
	clear_hook_input &&
	echo "D new message again" > newmsg &&
	git commit --no-post-rewrite -Fnewmsg --amend &&
	test ! -f post-rewrite.args &&
	test ! -f post-rewrite.data
'

test_expect_success 'git rebase --apply' '
	git reset --hard D &&
	clear_hook_input &&
	test_must_fail git rebase --apply --onto A B &&
	echo C > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase --apply --skip' '
	git reset --hard D &&
	clear_hook_input &&
	test_must_fail git rebase --apply --onto A B &&
	test_must_fail git rebase --skip &&
	echo D > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase --apply --skip the last one' '
	git reset --hard F &&
	clear_hook_input &&
	test_must_fail git rebase --apply --onto D A &&
	git rebase --skip &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse E) $(git rev-parse HEAD)
	$(git rev-parse F) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase -m' '
	git reset --hard D &&
	clear_hook_input &&
	test_must_fail git rebase -m --onto A B &&
	echo C > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase -m --skip' '
	git reset --hard D &&
	clear_hook_input &&
	test_must_fail git rebase -m --onto A B &&
	test_must_fail git rebase --skip &&
	echo D > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase with implicit use of merge backend' '
	git reset --hard D &&
	clear_hook_input &&
	test_must_fail git rebase --keep-empty --onto A B &&
	echo C > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase --skip with implicit use of merge backend' '
	git reset --hard D &&
	clear_hook_input &&
	test_must_fail git rebase --keep-empty --onto A B &&
	test_must_fail git rebase --skip &&
	echo D > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

. "$TEST_DIRECTORY"/lib-rebase.sh

set_fake_editor

# Helper to work around the lack of one-shot exporting for
# test_must_fail (as it is a shell function)
test_fail_interactive_rebase () {
	(
		FAKE_LINES="$1" &&
		shift &&
		export FAKE_LINES &&
		test_must_fail git rebase -i "$@"
	)
}

test_expect_success 'git rebase with failed pick' '
	clear_hook_input &&
	cat >todo <<-\EOF &&
	exec >bar
	merge -C merge-E E
	exec >G
	pick G
	exec >H 2>I
	pick H
	fixup I
	EOF

	(
		set_replace_editor todo &&
		test_must_fail git rebase -i D D 2>err
	) &&
	grep "would be overwritten" err &&
	rm bar &&

	test_must_fail git rebase --continue 2>err &&
	grep "would be overwritten" err &&
	rm G &&

	test_must_fail git rebase --continue 2>err &&
	grep "would be overwritten" err &&
	rm H &&

	test_must_fail git rebase --continue 2>err &&
	grep "would be overwritten" err &&
	rm I &&

	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse merge-E) $(git rev-parse HEAD~2)
	$(git rev-parse G) $(git rev-parse HEAD~1)
	$(git rev-parse H) $(git rev-parse HEAD)
	$(git rev-parse I) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase -i (unchanged)' '
	git reset --hard D &&
	clear_hook_input &&
	test_fail_interactive_rebase "1 2" --onto A B &&
	echo C > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase -i (skip)' '
	git reset --hard D &&
	clear_hook_input &&
	test_fail_interactive_rebase "2" --onto A B &&
	echo D > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase -i (squash)' '
	git reset --hard D &&
	clear_hook_input &&
	test_fail_interactive_rebase "1 squash 2" --onto A B &&
	echo C > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase -i (fixup without conflict)' '
	git reset --hard D &&
	clear_hook_input &&
	FAKE_LINES="1 fixup 2" git rebase -i B &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase -i (double edit)' '
	git reset --hard D &&
	clear_hook_input &&
	FAKE_LINES="edit 1 edit 2" git rebase -i B &&
	git rebase --continue &&
	echo something > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase -i (exec)' '
	git reset --hard D &&
	clear_hook_input &&
	FAKE_LINES="edit 1 exec_false 2" git rebase -i B &&
	echo something >bar &&
	git add bar &&
	# Fails because of exec false
	test_must_fail git rebase --continue &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_done
