#!/bin/sh

test_description="Test git-clean performance"

. ./perf-lib.sh

test_perf_default_repo
test_checkout_worktree

test_expect_success 'setup untracked directory with many sub dirs' '
	rm -rf 500_sub_dirs 100000_sub_dirs clean_test_dir &&
	mkdir 500_sub_dirs 100000_sub_dirs clean_test_dir &&
	for i in $(test_seq 1 500)
	do
		mkdir 500_sub_dirs/dir$i || return $?
	done &&
	for i in $(test_seq 1 200)
	do
		cp -r 500_sub_dirs 100000_sub_dirs/dir$i || return $?
	done
'

test_perf 'clean many untracked sub dirs, check for nested git' '
	git clean -n -q -f -d 100000_sub_dirs/
'

test_perf 'clean many untracked sub dirs, ignore nested git' '
	git clean -n -q -f -f -d 100000_sub_dirs/
'

test_perf 'ls-files -o' '
	git ls-files -o
'

test_done
