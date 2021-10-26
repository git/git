#!/bin/sh
#
# This test measures the performance of adding new files to the object database
# and index. The test was originally added to measure the effect of the
# core.fsyncObjectFiles=batch mode, which is why we are testing different values
# of that setting explicitly and creating a lot of unique objects.

test_description="Tests performance of stash"

. ./perf-lib.sh

. $TEST_DIRECTORY/lib-unique-files.sh

test_perf_default_repo
test_checkout_worktree

dir_count=10
files_per_dir=50
total_files=$((dir_count * files_per_dir))

# We need to create the files each time we run the perf test, but
# we do not want to measure the cost of creating the files, so run
# the tet once.
if test "${GIT_PERF_REPEAT_COUNT-1}" -ne 1
then
	echo "warning: Setting GIT_PERF_REPEAT_COUNT=1" >&2
	GIT_PERF_REPEAT_COUNT=1
fi

for m in false true batch
do
	test_expect_success "create the files for core.fsyncObjectFiles=$m" '
		git reset --hard &&
		# create files across directories
		test_create_unique_files $dir_count $files_per_dir files
	'

	# We only stash files in the 'files' subdirectory since
	# the perf test infrastructure creates files in the
	# current working directory that need to be preserved
	test_perf "stash 500 files (core.fsyncObjectFiles=$m)" "
		git -c core.fsyncobjectfiles=$m stash push -u -- files
	"
done

test_done
