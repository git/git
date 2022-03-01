#!/bin/sh

test_description='commit graph with 64-bit timestamps'
. ./test-lib.sh

if ! test_have_prereq TIME_IS_64BIT || ! test_have_prereq TIME_T_IS_64BIT
then
	skip_all='skipping 64-bit timestamp tests'
	test_done
fi

. "$TEST_DIRECTORY"/lib-commit-graph.sh

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

test_done
