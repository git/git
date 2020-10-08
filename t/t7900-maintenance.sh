#!/bin/sh

test_description='git maintenance builtin'

. ./test-lib.sh

GIT_TEST_COMMIT_GRAPH=0

test_expect_success 'help text' '
	test_expect_code 129 git maintenance -h 2>err &&
	test_i18ngrep "usage: git maintenance run" err &&
	test_expect_code 128 git maintenance barf 2>err &&
	test_i18ngrep "invalid subcommand: barf" err &&
	test_expect_code 129 git maintenance 2>err &&
	test_i18ngrep "usage: git maintenance" err
'

test_expect_success 'run [--auto|--quiet]' '
	GIT_TRACE2_EVENT="$(pwd)/run-no-auto.txt" \
		git maintenance run 2>/dev/null &&
	GIT_TRACE2_EVENT="$(pwd)/run-auto.txt" \
		git maintenance run --auto 2>/dev/null &&
	GIT_TRACE2_EVENT="$(pwd)/run-no-quiet.txt" \
		git maintenance run --no-quiet 2>/dev/null &&
	test_subcommand git gc --quiet <run-no-auto.txt &&
	test_subcommand ! git gc --auto --quiet <run-auto.txt &&
	test_subcommand git gc --no-quiet <run-no-quiet.txt
'

test_expect_success 'maintenance.<task>.enabled' '
	git config maintenance.gc.enabled false &&
	git config maintenance.commit-graph.enabled true &&
	GIT_TRACE2_EVENT="$(pwd)/run-config.txt" git maintenance run 2>err &&
	test_subcommand ! git gc --quiet <run-config.txt &&
	test_subcommand git commit-graph write --split --reachable --no-progress <run-config.txt
'

test_expect_success 'run --task=<task>' '
	GIT_TRACE2_EVENT="$(pwd)/run-commit-graph.txt" \
		git maintenance run --task=commit-graph 2>/dev/null &&
	GIT_TRACE2_EVENT="$(pwd)/run-gc.txt" \
		git maintenance run --task=gc 2>/dev/null &&
	GIT_TRACE2_EVENT="$(pwd)/run-commit-graph.txt" \
		git maintenance run --task=commit-graph 2>/dev/null &&
	GIT_TRACE2_EVENT="$(pwd)/run-both.txt" \
		git maintenance run --task=commit-graph --task=gc 2>/dev/null &&
	test_subcommand ! git gc --quiet <run-commit-graph.txt &&
	test_subcommand git gc --quiet <run-gc.txt &&
	test_subcommand git gc --quiet <run-both.txt &&
	test_subcommand git commit-graph write --split --reachable --no-progress <run-commit-graph.txt &&
	test_subcommand ! git commit-graph write --split --reachable --no-progress <run-gc.txt &&
	test_subcommand git commit-graph write --split --reachable --no-progress <run-both.txt
'

test_expect_success 'commit-graph auto condition' '
	COMMAND="maintenance run --task=commit-graph --auto --quiet" &&

	GIT_TRACE2_EVENT="$(pwd)/cg-no.txt" \
		git -c maintenance.commit-graph.auto=1 $COMMAND &&
	GIT_TRACE2_EVENT="$(pwd)/cg-negative-means-yes.txt" \
		git -c maintenance.commit-graph.auto="-1" $COMMAND &&

	test_commit first &&

	GIT_TRACE2_EVENT="$(pwd)/cg-zero-means-no.txt" \
		git -c maintenance.commit-graph.auto=0 $COMMAND &&
	GIT_TRACE2_EVENT="$(pwd)/cg-one-satisfied.txt" \
		git -c maintenance.commit-graph.auto=1 $COMMAND &&

	git commit --allow-empty -m "second" &&
	git commit --allow-empty -m "third" &&

	GIT_TRACE2_EVENT="$(pwd)/cg-two-satisfied.txt" \
		git -c maintenance.commit-graph.auto=2 $COMMAND &&

	COMMIT_GRAPH_WRITE="git commit-graph write --split --reachable --no-progress" &&
	test_subcommand ! $COMMIT_GRAPH_WRITE <cg-no.txt &&
	test_subcommand $COMMIT_GRAPH_WRITE <cg-negative-means-yes.txt &&
	test_subcommand ! $COMMIT_GRAPH_WRITE <cg-zero-means-no.txt &&
	test_subcommand $COMMIT_GRAPH_WRITE <cg-one-satisfied.txt &&
	test_subcommand $COMMIT_GRAPH_WRITE <cg-two-satisfied.txt
'

test_expect_success 'run --task=bogus' '
	test_must_fail git maintenance run --task=bogus 2>err &&
	test_i18ngrep "is not a valid task" err
'

test_expect_success 'run --task duplicate' '
	test_must_fail git maintenance run --task=gc --task=gc 2>err &&
	test_i18ngrep "cannot be selected multiple times" err
'

test_done
