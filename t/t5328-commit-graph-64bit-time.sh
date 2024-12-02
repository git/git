#!/bin/sh

test_description='commit graph with 64-bit timestamps'

. ./test-lib.sh

if ! test_have_prereq TIME_IS_64BIT || ! test_have_prereq TIME_T_IS_64BIT
then
	skip_all='skipping 64-bit timestamp tests'
	test_done
fi

. "$TEST_DIRECTORY"/lib-commit-graph.sh
. "$TEST_DIRECTORY/lib-chunk.sh"

UNIX_EPOCH_ZERO="@0 +0000"
FUTURE_DATE="@4147483646 +0000"

GIT_TEST_COMMIT_GRAPH_CHANGED_PATHS=0

test_expect_success 'lower layers have overflow chunk' '
	rm -f .git/objects/info/commit-graph &&
	test_commit --date "$FUTURE_DATE" future-1 &&
	test_commit --date "$UNIX_EPOCH_ZERO" old-1 &&
	git commit-graph write --reachable &&
	test_commit --date "$FUTURE_DATE" future-2 &&
	test_commit --date "$UNIX_EPOCH_ZERO" old-2 &&
	git commit-graph write --reachable --split=no-merge &&
	test_commit extra &&
	git commit-graph write --reachable --split=no-merge &&
	git commit-graph write --reachable &&
	graph_read_expect 5 "generation_data generation_data_overflow" &&
	mv .git/objects/info/commit-graph commit-graph-upgraded &&
	git commit-graph write --reachable &&
	graph_read_expect 5 "generation_data generation_data_overflow" &&
	test_cmp .git/objects/info/commit-graph commit-graph-upgraded
'

graph_git_behavior 'overflow' '' HEAD~2 HEAD

test_expect_success 'set up and verify repo with generation data overflow chunk' '
	git init repo &&
	(
		cd repo &&
		test_commit --date "$UNIX_EPOCH_ZERO" 1 &&
		test_commit 2 &&
		test_commit --date "$UNIX_EPOCH_ZERO" 3 &&
		git commit-graph write --reachable &&
		graph_read_expect 3 generation_data &&
		test_commit --date "$FUTURE_DATE" 4 &&
		test_commit 5 &&
		test_commit --date "$UNIX_EPOCH_ZERO" 6 &&
		git branch left &&
		git reset --hard 3 &&
		test_commit 7 &&
		test_commit --date "$FUTURE_DATE" 8 &&
		test_commit 9 &&
		git branch right &&
		git reset --hard 3 &&
		test_merge M left right &&
		git commit-graph write --reachable &&
		graph_read_expect 10 "generation_data generation_data_overflow" &&
		git commit-graph verify
	)
'

graph_git_behavior 'overflow 2' repo left right

test_expect_success 'single commit with generation data exceeding UINT32_MAX' '
	git init repo-uint32-max &&
	test_commit -C repo-uint32-max --date "@4294967297 +0000" 1 &&
	git -C repo-uint32-max commit-graph write --reachable &&
	graph_read_expect -C repo-uint32-max 1 "generation_data" &&
	git -C repo-uint32-max commit-graph verify
'

test_expect_success 'reader notices out-of-bounds generation overflow' '
	graph=.git/objects/info/commit-graph &&
	test_when_finished "rm -rf $graph" &&
	git commit-graph write --reachable &&
	corrupt_chunk_file $graph GDO2 clear &&
	test_must_fail git log 2>err &&
	grep "commit-graph overflow generation data is too small" err
'

test_done
