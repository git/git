#!/bin/sh

test_description='git checkout --patch'
. ./lib-patch-mode.sh

test_expect_success 'setup' '
	mkdir dir &&
	echo parent > dir/foo &&
	echo dummy > bar &&
	git add bar dir/foo &&
	git commit -m initial &&
	test_tick &&
	test_commit second dir/foo head &&
	echo index > dir/foo &&
	git add dir/foo &&
	set_and_save_state bar bar_work bar_index &&
	save_head
'

# note: bar sorts before dir, so the first 'n' is always to skip 'bar'

test_expect_success 'saying "n" does nothing' '
	set_state dir/foo work index
	(echo n; echo n) | test_must_fail git stash save -p &&
	verify_state dir/foo work index &&
	verify_saved_state bar
'

test_expect_success 'git stash -p' '
	(echo n; echo y) | git stash save -p &&
	verify_state dir/foo head index &&
	verify_saved_state bar &&
	git reset --hard &&
	git stash apply &&
	verify_state dir/foo work head &&
	verify_state bar dummy dummy
'

test_expect_success 'git stash -p --no-keep-index' '
	set_state dir/foo work index &&
	set_state bar bar_work bar_index &&
	(echo n; echo y) | git stash save -p --no-keep-index &&
	verify_state dir/foo head head &&
	verify_state bar bar_work dummy &&
	git reset --hard &&
	git stash apply --index &&
	verify_state dir/foo work index &&
	verify_state bar dummy bar_index
'

test_expect_success 'none of this moved HEAD' '
	verify_saved_head
'

test_done
