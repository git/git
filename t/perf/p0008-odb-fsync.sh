#!/bin/sh
#
# This test measures the performance of adding new files to the object
# database. The test was originally added to measure the effect of the
# core.fsyncMethod=batch mode, which is why we are testing different values of
# that setting explicitly and creating a lot of unique objects.

test_description="Tests performance of adding things to the object database"

. ./perf-lib.sh

. $TEST_DIRECTORY/lib-unique-files.sh

test_perf_fresh_repo
test_checkout_worktree

dir_count=10
files_per_dir=50
total_files=$((dir_count * files_per_dir))

populate_files () {
	test_create_unique_files $dir_count $files_per_dir files
}

setup_repo () {
	(rm -rf .git || 1) &&
	git init &&
	test_commit first &&
	populate_files
}

test_perf_fsync_cfgs () {
	local method &&
	local cfg &&
	for method in none fsync batch writeout-only
	do
		case $method in
		none)
			cfg="-c core.fsync=none"
			;;
		*)
			cfg="-c core.fsync=loose-object -c core.fsyncMethod=$method"
		esac &&

		# Set GIT_TEST_FSYNC=1 explicitly since fsync is normally
		# disabled by t/test-lib.sh.
		if ! test_perf "$1 (fsyncMethod=$method)" \
						--setup "$2" \
						"GIT_TEST_FSYNC=1 git $cfg $3"
		then
			break
		fi
	done
}

test_perf_fsync_cfgs "add $total_files files" \
	"setup_repo" \
	"add -- files"

test_perf_fsync_cfgs "stash $total_files files" \
	"setup_repo" \
	"stash push -u -- files"

test_perf_fsync_cfgs "unpack $total_files files" \
	"
	setup_repo &&
	git -c core.fsync=none add -- files &&
	git -c core.fsync=none commit -q -m second &&
	echo HEAD | git pack-objects -q --stdout --revs >test_pack.pack &&
	setup_repo
	" \
	"unpack-objects -q <test_pack.pack"

test_perf_fsync_cfgs "commit $total_files files" \
	"
	setup_repo &&
	git -c core.fsync=none add -- files &&
	populate_files
	" \
	"commit -q -a -m test"

test_done
