#!/bin/sh

test_description="remember regular & dir renames in sequence of merges"

. ./test-lib.sh

#
# NOTE 1: this testfile tends to not only rename files, but modify on both
#         sides; without modifying on both sides, optimizations can kick in
#         which make rename detection irrelevant or trivial.  We want to make
#         sure that we are triggering rename caching rather than rename
#         bypassing.
#
# NOTE 2: this testfile uses 'test-tool fast-rebase' instead of either
#         cherry-pick or rebase.  sequencer.c is only superficially
#         integrated with merge-ort; it calls merge_switch_to_result()
#         after EACH merge, which updates the index and working copy AND
#         throws away the cached results (because merge_switch_to_result()
#         is only supposed to be called at the end of the sequence).
#         Integrating them more deeply is a big task, so for now the tests
#         use 'test-tool fast-rebase'.
#


#
# In the following simple testcase:
#   Base:     numbers_1, values_1
#   Upstream: numbers_2, values_2
#   Topic_1:  sequence_3
#   Topic_2:  scruples_3
# or, in english, rename numbers -> sequence in the first cummit, and rename
# values -> scruples in the second cummit.
#
# This shouldn't be a challenge, it's just verifying that cached renames isn't
# preventing us from finding new renames.
#
test_expect_success 'caching renames does not preclude finding new ones' '
	test_create_repo caching-renames-and-new-renames &&
	(
		cd caching-renames-and-new-renames &&

		test_seq 2 10 >numbers &&
		test_seq 2 10 >values &&
		but add numbers values &&
		but cummit -m orig &&

		but branch upstream &&
		but branch topic &&

		but switch upstream &&
		test_seq 1 10 >numbers &&
		test_seq 1 10 >values &&
		but add numbers values &&
		but cummit -m "Tweaked both files" &&

		but switch topic &&

		test_seq 2 12 >numbers &&
		but add numbers &&
		but mv numbers sequence &&
		but cummit -m A &&

		test_seq 2 12 >values &&
		but add values &&
		but mv values scruples &&
		but cummit -m B &&

		#
		# Actual testing
		#

		but switch upstream &&

		test-tool fast-rebase --onto HEAD upstream~1 topic &&
		#but cherry-pick upstream~1..topic

		but ls-files >tracked-files &&
		test_line_count = 2 tracked-files &&
		test_seq 1 12 >expect &&
		test_cmp expect sequence &&
		test_cmp expect scruples
	)
'

#
# In the following testcase:
#   Base:     numbers_1
#   Upstream: rename numbers_1 -> sequence_2
#   Topic_1:  numbers_3
#   Topic_2:  numbers_1
# or, in english, the first cummit on the topic branch modifies numbers by
# shrinking it (dramatically) and the second cummit on topic reverts its
# parent.
#
# Can but apply both patches?
#
# Traditional cherry-pick/rebase will fail to apply the second cummit, the
# one that reverted its parent, because despite detecting the rename from
# 'numbers' to 'sequence' for the first cummit, it fails to detect that
# rename when picking the second cummit.  That's "reasonable" given the
# dramatic change in size of the file, but remembering the rename and
# reusing it is reasonable too.
#
# We do test here that we expect rename detection to only be run once total
# (the topic side of history doesn't need renames, and with caching we
# should be able to only run rename detection on the upstream side one
# time.)
test_expect_success 'cherry-pick both a cummit and its immediate revert' '
	test_create_repo pick-cummit-and-its-immediate-revert &&
	(
		cd pick-cummit-and-its-immediate-revert &&

		test_seq 11 30 >numbers &&
		but add numbers &&
		but cummit -m orig &&

		but branch upstream &&
		but branch topic &&

		but switch upstream &&
		test_seq 1 30 >numbers &&
		but add numbers &&
		but mv numbers sequence &&
		but cummit -m "Renamed (and modified) numbers -> sequence" &&

		but switch topic &&

		test_seq 11 13 >numbers &&
		but add numbers &&
		but cummit -m A &&

		but revert HEAD &&

		#
		# Actual testing
		#

		but switch upstream &&

		GIT_TRACE2_PERF="$(pwd)/trace.output" &&
		export GIT_TRACE2_PERF &&

		test-tool fast-rebase --onto HEAD upstream~1 topic &&
		#but cherry-pick upstream~1..topic &&

		grep region_enter.*diffcore_rename trace.output >calls &&
		test_line_count = 1 calls
	)
'

#
# In the following testcase:
#   Base:     sequence_1
#   Upstream: rename sequence_1 -> values_2
#   Topic_1:  rename sequence_1 -> values_3
#   Topic_2:  add unrelated sequence_4
# or, in english, both sides rename sequence -> values, and then the second
# cummit on the topic branch adds an unrelated file called sequence.
#
# This testcase presents no problems for but traditionally, but having both
# sides do the same rename in effect "uses it up" and if it remains cached,
# could cause a spurious rename/add conflict.
#
test_expect_success 'rename same file identically, then reintroduce it' '
	test_create_repo rename-rename-1to1-then-add-old-filename &&
	(
		cd rename-rename-1to1-then-add-old-filename &&

		test_seq 3 8 >sequence &&
		but add sequence &&
		but cummit -m orig &&

		but branch upstream &&
		but branch topic &&

		but switch upstream &&
		test_seq 1 8 >sequence &&
		but add sequence &&
		but mv sequence values &&
		but cummit -m "Renamed (and modified) sequence -> values" &&

		but switch topic &&

		test_seq 3 10 >sequence &&
		but add sequence &&
		but mv sequence values &&
		but cummit -m A &&

		test_write_lines A B C D E F G H I J >sequence &&
		but add sequence &&
		but cummit -m B &&

		#
		# Actual testing
		#

		but switch upstream &&

		GIT_TRACE2_PERF="$(pwd)/trace.output" &&
		export GIT_TRACE2_PERF &&

		test-tool fast-rebase --onto HEAD upstream~1 topic &&
		#but cherry-pick upstream~1..topic &&

		but ls-files >tracked &&
		test_line_count = 2 tracked &&
		test_path_is_file values &&
		test_path_is_file sequence &&

		grep region_enter.*diffcore_rename trace.output >calls &&
		test_line_count = 2 calls
	)
'

#
# In the following testcase:
#   Base:     olddir/{valuesZ_1, valuesY_1, valuesX_1}
#   Upstream: rename olddir/valuesZ_1 -> dirA/valuesZ_2
#             rename olddir/valuesY_1 -> dirA/valuesY_2
#             rename olddir/valuesX_1 -> dirB/valuesX_2
#   Topic_1:  rename olddir/valuesZ_1 -> dirA/valuesZ_3
#             rename olddir/valuesY_1 -> dirA/valuesY_3
#   Topic_2:  add olddir/newfile
#   Expected Pick1: dirA/{valuesZ, valuesY}, dirB/valuesX
#   Expected Pick2: dirA/{valuesZ, valuesY}, dirB/{valuesX, newfile}
#
# This testcase presents no problems for but traditionally, but having both
# sides do the same renames in effect "use it up" but if the renames remain
# cached, the directory rename could put newfile in the wrong directory.
#
test_expect_success 'rename same file identically, then add file to old dir' '
	test_create_repo rename-rename-1to1-then-add-file-to-old-dir &&
	(
		cd rename-rename-1to1-then-add-file-to-old-dir &&

		mkdir olddir/ &&
		test_seq 3 8 >olddir/valuesZ &&
		test_seq 3 8 >olddir/valuesY &&
		test_seq 3 8 >olddir/valuesX &&
		but add olddir &&
		but cummit -m orig &&

		but branch upstream &&
		but branch topic &&

		but switch upstream &&
		test_seq 1 8 >olddir/valuesZ &&
		test_seq 1 8 >olddir/valuesY &&
		test_seq 1 8 >olddir/valuesX &&
		but add olddir &&
		mkdir dirA &&
		but mv olddir/valuesZ olddir/valuesY dirA &&
		but mv olddir/ dirB/ &&
		but cummit -m "Renamed (and modified) values*" &&

		but switch topic &&

		test_seq 3 10 >olddir/valuesZ &&
		test_seq 3 10 >olddir/valuesY &&
		but add olddir &&
		mkdir dirA &&
		but mv olddir/valuesZ olddir/valuesY dirA &&
		but cummit -m A &&

		>olddir/newfile &&
		but add olddir/newfile &&
		but cummit -m B &&

		#
		# Actual testing
		#

		but switch upstream &&
		but config merge.directoryRenames true &&

		GIT_TRACE2_PERF="$(pwd)/trace.output" &&
		export GIT_TRACE2_PERF &&

		test-tool fast-rebase --onto HEAD upstream~1 topic &&
		#but cherry-pick upstream~1..topic &&

		but ls-files >tracked &&
		test_line_count = 4 tracked &&
		test_path_is_file dirA/valuesZ &&
		test_path_is_file dirA/valuesY &&
		test_path_is_file dirB/valuesX &&
		test_path_is_file dirB/newfile &&

		grep region_enter.*diffcore_rename trace.output >calls &&
		test_line_count = 3 calls
	)
'

#
# In the following testcase, upstream renames a directory, and the topic branch
# first adds a file to the directory, then later renames the directory
# differently:
#   Base:     olddir/a
#             olddir/b
#   Upstream: rename olddir/ -> newdir/
#   Topic_1:  add olddir/newfile
#   Topic_2:  rename olddir/ -> otherdir/
#
# Here we are just concerned that cached renames might prevent us from seeing
# the rename conflict, and we want to ensure that we do get a conflict.
#
# While at it, though, we do test that we only try to detect renames 2
# times and not three.  (The first merge needs to detect renames on the
# upstream side.  Traditionally, the second merge would need to detect
# renames on both sides of history, but our caching of upstream renames
# should avoid the need to re-detect upstream renames.)
#
test_expect_success 'cached dir rename does not prevent noticing later conflict' '
	test_create_repo dir-rename-cache-not-occluding-later-conflict &&
	(
		cd dir-rename-cache-not-occluding-later-conflict &&

		mkdir olddir &&
		test_seq 3 10 >olddir/a &&
		test_seq 3 10 >olddir/b &&
		but add olddir &&
		but cummit -m orig &&

		but branch upstream &&
		but branch topic &&

		but switch upstream &&
		test_seq 3 10 >olddir/a &&
		test_seq 3 10 >olddir/b &&
		but add olddir &&
		but mv olddir newdir &&
		but cummit -m "Dir renamed" &&

		but switch topic &&

		>olddir/newfile &&
		but add olddir/newfile &&
		but cummit -m A &&

		test_seq 1 8 >olddir/a &&
		test_seq 1 8 >olddir/b &&
		but add olddir &&
		but mv olddir otherdir &&
		but cummit -m B &&

		#
		# Actual testing
		#

		but switch upstream &&
		but config merge.directoryRenames true &&

		GIT_TRACE2_PERF="$(pwd)/trace.output" &&
		export GIT_TRACE2_PERF &&

		test_must_fail test-tool fast-rebase --onto HEAD upstream~1 topic >output &&
		#but cherry-pick upstream..topic &&

		grep CONFLICT..rename/rename output &&

		grep region_enter.*diffcore_rename trace.output >calls &&
		test_line_count = 2 calls
	)
'

# Helper for the next two tests
test_setup_upstream_rename () {
	test_create_repo $1 &&
	(
		cd $1 &&

		test_seq 3 8 >somefile &&
		test_seq 3 8 >relevant-rename &&
		but add somefile relevant-rename &&
		mkdir olddir &&
		test_write_lines a b c d e f g >olddir/a &&
		test_write_lines z y x w v u t >olddir/b &&
		but add olddir &&
		but cummit -m orig &&

		but branch upstream &&
		but branch topic &&

		but switch upstream &&
		test_seq 1 8 >somefile &&
		test_seq 1 8 >relevant-rename &&
		but add somefile relevant-rename &&
		but mv relevant-rename renamed &&
		echo h >>olddir/a &&
		echo s >>olddir/b &&
		but add olddir &&
		but mv olddir newdir &&
		but cummit -m "Dir renamed"
	)
}

#
# In the following testcase, upstream renames a file in the toplevel directory
# as well as its only directory:
#   Base:     relevant-rename_1
#             somefile
#             olddir/a
#             olddir/b
#   Upstream: rename relevant-rename_1 -> renamed_2
#             rename olddir/           -> newdir/
#   Topic_1:  relevant-rename_3
#   Topic_2:  olddir/newfile_1
#   Topic_3:  olddir/newfile_2
#
# In this testcase, since the first cummit being picked only modifies a
# file in the toplevel directory, the directory rename is irrelevant for
# that first merge.  However, we need to notice the directory rename for
# the merge that picks the second cummit, and we don't want the third
# cummit to mess up its location either.  We want to make sure that
# olddir/newfile doesn't exist in the result and that newdir/newfile does.
#
# We also test that we only do rename detection twice.  We never need
# rename detection on the topic side of history, but we do need it twice on
# the upstream side of history.  For the first topic cummit, we only need
# the
#   relevant-rename -> renamed
# rename, because olddir is unmodified by Topic_1.  For Topic_2, however,
# the new file being added to olddir means files that were previously
# irrelevant for rename detection are now relevant, forcing us to repeat
# rename detection for the paths we don't already have cached.  Topic_3 also
# tweaks olddir/newfile, but the renames in olddir/ will have been cached
# from the second rename detection run.
#
test_expect_success 'dir rename unneeded, then add new file to old dir' '
	test_setup_upstream_rename dir-rename-unneeded-until-new-file &&
	(
		cd dir-rename-unneeded-until-new-file &&

		but switch topic &&

		test_seq 3 10 >relevant-rename &&
		but add relevant-rename &&
		but cummit -m A &&

		echo foo >olddir/newfile &&
		but add olddir/newfile &&
		but cummit -m B &&

		echo bar >>olddir/newfile &&
		but add olddir/newfile &&
		but cummit -m C &&

		#
		# Actual testing
		#

		but switch upstream &&
		but config merge.directoryRenames true &&

		GIT_TRACE2_PERF="$(pwd)/trace.output" &&
		export GIT_TRACE2_PERF &&

		test-tool fast-rebase --onto HEAD upstream~1 topic &&
		#but cherry-pick upstream..topic &&

		grep region_enter.*diffcore_rename trace.output >calls &&
		test_line_count = 2 calls &&

		but ls-files >tracked &&
		test_line_count = 5 tracked &&
		test_path_is_missing olddir/newfile &&
		test_path_is_file newdir/newfile
	)
'

#
# The following testcase is *very* similar to the last one, but instead of
# adding a new olddir/newfile, it renames somefile -> olddir/newfile:
#   Base:     relevant-rename_1
#             somefile_1
#             olddir/a
#             olddir/b
#   Upstream: rename relevant-rename_1 -> renamed_2
#             rename olddir/           -> newdir/
#   Topic_1:  relevant-rename_3
#   Topic_2:  rename somefile -> olddir/newfile_2
#   Topic_3:  modify olddir/newfile_3
#
# In this testcase, since the first cummit being picked only modifies a
# file in the toplevel directory, the directory rename is irrelevant for
# that first merge.  However, we need to notice the directory rename for
# the merge that picks the second cummit, and we don't want the third
# cummit to mess up its location either.  We want to make sure that
# neither somefile or olddir/newfile exists in the result and that
# newdir/newfile does.
#
# This testcase needs one more call to rename detection than the last
# testcase, because of the somefile -> olddir/newfile rename in Topic_2.
test_expect_success 'dir rename unneeded, then rename existing file into old dir' '
	test_setup_upstream_rename dir-rename-unneeded-until-file-moved-inside &&
	(
		cd dir-rename-unneeded-until-file-moved-inside &&

		but switch topic &&

		test_seq 3 10 >relevant-rename &&
		but add relevant-rename &&
		but cummit -m A &&

		test_seq 1 10 >somefile &&
		but add somefile &&
		but mv somefile olddir/newfile &&
		but cummit -m B &&

		test_seq 1 12 >olddir/newfile &&
		but add olddir/newfile &&
		but cummit -m C &&

		#
		# Actual testing
		#

		but switch upstream &&
		but config merge.directoryRenames true &&

		GIT_TRACE2_PERF="$(pwd)/trace.output" &&
		export GIT_TRACE2_PERF &&

		test-tool fast-rebase --onto HEAD upstream~1 topic &&
		#but cherry-pick upstream..topic &&

		grep region_enter.*diffcore_rename trace.output >calls &&
		test_line_count = 3 calls &&

		test_path_is_missing somefile &&
		test_path_is_missing olddir/newfile &&
		test_path_is_file newdir/newfile &&
		but ls-files >tracked &&
		test_line_count = 4 tracked
	)
'

# Helper for the next two tests
test_setup_topic_rename () {
	test_create_repo $1 &&
	(
		cd $1 &&

		test_seq 3 8 >somefile &&
		mkdir olddir &&
		test_seq 3 8 >olddir/a &&
		echo b >olddir/b &&
		but add olddir somefile &&
		but cummit -m orig &&

		but branch upstream &&
		but branch topic &&

		but switch topic &&
		test_seq 1 8 >somefile &&
		test_seq 1 8 >olddir/a &&
		but add somefile olddir/a &&
		but mv olddir newdir &&
		but cummit -m "Dir renamed" &&

		test_seq 1 10 >somefile &&
		but add somefile &&
		mkdir olddir &&
		>olddir/unrelated-file &&
		but add olddir &&
		but cummit -m "Unrelated file in recreated old dir"
	)
}

#
# In the following testcase, the first cummit on the topic branch renames
# a directory, while the second recreates the old directory and places a
# file into it:
#   Base:     somefile
#             olddir/a
#             olddir/b
#   Upstream: olddir/newfile
#   Topic_1:  somefile_2
#             rename olddir/ -> newdir/
#   Topic_2:  olddir/unrelated-file
#
# Note that the first pick should merge:
#   Base:     somefile
#             olddir/{a,b}
#   Upstream: olddir/newfile
#   Topic_1:  rename olddir/ -> newdir/
# For which the expected result (assuming merge.directoryRenames=true) is
# clearly:
#   Result:   somefile
#             newdir/{a, b, newfile}
#
# While the second pick does the following three-way merge:
#   Base (Topic_1):           somefile
#                             newdir/{a,b}
#   Upstream (Result from 1): same files as base, but adds newdir/newfile
#   Topic_2:                  same files as base, but adds olddir/unrelated-file
#
# The second merge is pretty trivial; upstream adds newdir/newfile, and
# topic_2 adds olddir/unrelated-file.  We're just testing that we don't
# accidentally cache directory renames somehow and rename
# olddir/unrelated-file to newdir/unrelated-file.
#
# This testcase should only need one call to diffcore_rename_extended().
test_expect_success 'caching renames only on upstream side, part 1' '
	test_setup_topic_rename cache-renames-only-upstream-add-file &&
	(
		cd cache-renames-only-upstream-add-file &&

		but switch upstream &&

		>olddir/newfile &&
		but add olddir/newfile &&
		but cummit -m "Add newfile" &&

		#
		# Actual testing
		#

		but switch upstream &&

		but config merge.directoryRenames true &&

		GIT_TRACE2_PERF="$(pwd)/trace.output" &&
		export GIT_TRACE2_PERF &&

		test-tool fast-rebase --onto HEAD upstream~1 topic &&
		#but cherry-pick upstream..topic &&

		grep region_enter.*diffcore_rename trace.output >calls &&
		test_line_count = 1 calls &&

		but ls-files >tracked &&
		test_line_count = 5 tracked &&
		test_path_is_missing newdir/unrelated-file &&
		test_path_is_file olddir/unrelated-file &&
		test_path_is_file newdir/newfile &&
		test_path_is_file newdir/b &&
		test_path_is_file newdir/a &&
		test_path_is_file somefile
	)
'

#
# The following testcase is *very* similar to the last one, but instead of
# adding a new olddir/newfile, it renames somefile -> olddir/newfile:
#   Base:     somefile
#             olddir/a
#             olddir/b
#   Upstream: somefile_1 -> olddir/newfile
#   Topic_1:  rename olddir/ -> newdir/
#             somefile_2
#   Topic_2:  olddir/unrelated-file
#             somefile_3
#
# Much like the previous test, this case is actually trivial and we are just
# making sure there isn't some spurious directory rename caching going on
# for the wrong side of history.
#
#
# This testcase should only need two calls to diffcore_rename_extended(),
# both for the first merge, one for each side of history.
#
test_expect_success 'caching renames only on upstream side, part 2' '
	test_setup_topic_rename cache-renames-only-upstream-rename-file &&
	(
		cd cache-renames-only-upstream-rename-file &&

		but switch upstream &&

		but mv somefile olddir/newfile &&
		but cummit -m "Add newfile" &&

		#
		# Actual testing
		#

		but switch upstream &&

		but config merge.directoryRenames true &&

		GIT_TRACE2_PERF="$(pwd)/trace.output" &&
		export GIT_TRACE2_PERF &&

		test-tool fast-rebase --onto HEAD upstream~1 topic &&
		#but cherry-pick upstream..topic &&

		grep region_enter.*diffcore_rename trace.output >calls &&
		test_line_count = 2 calls &&

		but ls-files >tracked &&
		test_line_count = 4 tracked &&
		test_path_is_missing newdir/unrelated-file &&
		test_path_is_file olddir/unrelated-file &&
		test_path_is_file newdir/newfile &&
		test_path_is_file newdir/b &&
		test_path_is_file newdir/a
	)
'

#
# The following testcase just creates two simple renames (slightly modified
# on both sides but without conflicting changes), and a directory full of
# files that are otherwise uninteresting.  The setup is as follows:
#
#   base:     unrelated/<BUNCH OF FILES>
#             numbers
#             values
#   upstream: modify: numbers
#             modify: values
#   topic:    add: unrelated/foo
#             modify: numbers
#             modify: values
#             rename: numbers -> sequence
#             rename: values -> progression
#
# This is a trivial rename case, but we're curious what happens with a very
# low renameLimit interacting with the restart optimization trying to notice
# that unrelated/ looks like a trivial merge candidate.
#
test_expect_success 'avoid assuming we detected renames' '
	but init redo-weirdness &&
	(
		cd redo-weirdness &&

		mkdir unrelated &&
		for i in $(test_seq 1 10)
		do
			>unrelated/$i
		done &&
		test_seq  2 10 >numbers &&
		test_seq 12 20 >values &&
		but add numbers values unrelated/ &&
		but cummit -m orig &&

		but branch upstream &&
		but branch topic &&

		but switch upstream &&
		test_seq  1 10 >numbers &&
		test_seq 11 20 >values &&
		but add numbers &&
		but cummit -m "Some tweaks" &&

		but switch topic &&

		>unrelated/foo &&
		test_seq  2 12 >numbers &&
		test_seq 12 22 >values &&
		but add numbers values unrelated/ &&
		but mv numbers sequence &&
		but mv values progression &&
		but cummit -m A &&

		#
		# Actual testing
		#

		but switch --detach topic^0 &&

		test_must_fail but -c merge.renameLimit=1 rebase upstream &&

		but ls-files -u >actual &&
		! test_file_is_empty actual
	)
'

test_done
