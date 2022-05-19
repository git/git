#!/bin/sh
#
# Copyright (c) 2010 Thomas Rast
#

test_description='Test the post-rewrite hook.'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_cummit A foo A &&
	test_cummit B foo B &&
	test_cummit C foo C &&
	test_cummit D foo D &&
	but checkout A^0 &&
	test_cummit E bar E &&
	test_cummit F foo F &&
	but checkout main &&

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

test_expect_success 'but cummit --amend' '
	clear_hook_input &&
	echo "D new message" > newmsg &&
	oldsha=$(but rev-parse HEAD^0) &&
	but cummit -Fnewmsg --amend &&
	echo amend > expected.args &&
	echo $oldsha $(but rev-parse HEAD^0) > expected.data &&
	verify_hook_input
'

test_expect_success 'but cummit --amend --no-post-rewrite' '
	clear_hook_input &&
	echo "D new message again" > newmsg &&
	but cummit --no-post-rewrite -Fnewmsg --amend &&
	test ! -f post-rewrite.args &&
	test ! -f post-rewrite.data
'

test_expect_success 'but rebase --apply' '
	but reset --hard D &&
	clear_hook_input &&
	test_must_fail but rebase --apply --onto A B &&
	echo C > foo &&
	but add foo &&
	but rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(but rev-parse C) $(but rev-parse HEAD^)
	$(but rev-parse D) $(but rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'but rebase --apply --skip' '
	but reset --hard D &&
	clear_hook_input &&
	test_must_fail but rebase --apply --onto A B &&
	test_must_fail but rebase --skip &&
	echo D > foo &&
	but add foo &&
	but rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(but rev-parse C) $(but rev-parse HEAD^)
	$(but rev-parse D) $(but rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'but rebase --apply --skip the last one' '
	but reset --hard F &&
	clear_hook_input &&
	test_must_fail but rebase --apply --onto D A &&
	but rebase --skip &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(but rev-parse E) $(but rev-parse HEAD)
	$(but rev-parse F) $(but rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'but rebase -m' '
	but reset --hard D &&
	clear_hook_input &&
	test_must_fail but rebase -m --onto A B &&
	echo C > foo &&
	but add foo &&
	but rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(but rev-parse C) $(but rev-parse HEAD^)
	$(but rev-parse D) $(but rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'but rebase -m --skip' '
	but reset --hard D &&
	clear_hook_input &&
	test_must_fail but rebase -m --onto A B &&
	test_must_fail but rebase --skip &&
	echo D > foo &&
	but add foo &&
	but rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(but rev-parse C) $(but rev-parse HEAD^)
	$(but rev-parse D) $(but rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'but rebase with implicit use of merge backend' '
	but reset --hard D &&
	clear_hook_input &&
	test_must_fail but rebase --keep-empty --onto A B &&
	echo C > foo &&
	but add foo &&
	but rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(but rev-parse C) $(but rev-parse HEAD^)
	$(but rev-parse D) $(but rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'but rebase --skip with implicit use of merge backend' '
	but reset --hard D &&
	clear_hook_input &&
	test_must_fail but rebase --keep-empty --onto A B &&
	test_must_fail but rebase --skip &&
	echo D > foo &&
	but add foo &&
	but rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(but rev-parse C) $(but rev-parse HEAD^)
	$(but rev-parse D) $(but rev-parse HEAD)
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
		test_must_fail but rebase -i "$@"
	)
}

test_expect_success 'but rebase -i (unchanged)' '
	but reset --hard D &&
	clear_hook_input &&
	test_fail_interactive_rebase "1 2" --onto A B &&
	echo C > foo &&
	but add foo &&
	but rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(but rev-parse C) $(but rev-parse HEAD^)
	$(but rev-parse D) $(but rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'but rebase -i (skip)' '
	but reset --hard D &&
	clear_hook_input &&
	test_fail_interactive_rebase "2" --onto A B &&
	echo D > foo &&
	but add foo &&
	but rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(but rev-parse D) $(but rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'but rebase -i (squash)' '
	but reset --hard D &&
	clear_hook_input &&
	test_fail_interactive_rebase "1 squash 2" --onto A B &&
	echo C > foo &&
	but add foo &&
	but rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(but rev-parse C) $(but rev-parse HEAD)
	$(but rev-parse D) $(but rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'but rebase -i (fixup without conflict)' '
	but reset --hard D &&
	clear_hook_input &&
	FAKE_LINES="1 fixup 2" but rebase -i B &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(but rev-parse C) $(but rev-parse HEAD)
	$(but rev-parse D) $(but rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'but rebase -i (double edit)' '
	but reset --hard D &&
	clear_hook_input &&
	FAKE_LINES="edit 1 edit 2" but rebase -i B &&
	but rebase --continue &&
	echo something > foo &&
	but add foo &&
	but rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(but rev-parse C) $(but rev-parse HEAD^)
	$(but rev-parse D) $(but rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'but rebase -i (exec)' '
	but reset --hard D &&
	clear_hook_input &&
	FAKE_LINES="edit 1 exec_false 2" but rebase -i B &&
	echo something >bar &&
	but add bar &&
	# Fails because of exec false
	test_must_fail but rebase --continue &&
	but rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(but rev-parse C) $(but rev-parse HEAD^)
	$(but rev-parse D) $(but rev-parse HEAD)
	EOF
	verify_hook_input
'

test_done
