#!/bin/sh

test_description='git reset --patch'
. ./lib-patch-mode.sh

test_expect_success PERL 'setup' '
	mkdir dir &&
	echo parent > dir/foo &&
	echo dummy > bar &&
	git add dir &&
	git commit -m initial &&
	test_tick &&
	test_commit second dir/foo head &&
	set_and_save_state bar bar_work bar_index &&
	save_head
'

# note: bar sorts before foo, so the first 'n' is always to skip 'bar'

test_expect_success PERL 'saying "n" does nothing' '
	set_and_save_state dir/foo work work &&
	test_write_lines n n | git reset -p &&
	verify_saved_state dir/foo &&
	verify_saved_state bar
'

test_expect_success PERL 'git reset -p' '
	test_write_lines n y | git reset -p >output &&
	verify_state dir/foo work head &&
	verify_saved_state bar &&
	test_i18ngrep "Unstage" output
'

test_expect_success PERL 'git reset -p HEAD^' '
	test_write_lines n y | git reset -p HEAD^ >output &&
	verify_state dir/foo work parent &&
	verify_saved_state bar &&
	test_i18ngrep "Apply" output
'

# The idea in the rest is that bar sorts first, so we always say 'y'
# first and if the path limiter fails it'll apply to bar instead of
# dir/foo.  There's always an extra 'n' to reject edits to dir/foo in
# the failure case (and thus get out of the loop).

test_expect_success PERL 'git reset -p dir' '
	set_state dir/foo work work &&
	test_write_lines y n | git reset -p dir &&
	verify_state dir/foo work head &&
	verify_saved_state bar
'

test_expect_success PERL 'git reset -p -- foo (inside dir)' '
	set_state dir/foo work work &&
	test_write_lines y n | (cd dir && git reset -p -- foo) &&
	verify_state dir/foo work head &&
	verify_saved_state bar
'

test_expect_success PERL 'git reset -p HEAD^ -- dir' '
	test_write_lines y n | git reset -p HEAD^ -- dir &&
	verify_state dir/foo work parent &&
	verify_saved_state bar
'

test_expect_success PERL 'none of this moved HEAD' '
	verify_saved_head
'


test_done
