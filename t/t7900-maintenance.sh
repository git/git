#!/bin/sh

test_description='git maintenance builtin'

GIT_TEST_COMMIT_GRAPH=0
GIT_TEST_MULTI_PACK_INDEX=0

. ./test-lib.sh

test_expect_success 'help text' '
	test_must_fail git maintenance -h 2>err &&
	test_i18ngrep "usage: git maintenance run" err
'

test_expect_success 'run [--auto|--quiet]' '
	GIT_TRACE2_EVENT="$(pwd)/run-no-auto.txt" git maintenance run --no-quiet &&
	GIT_TRACE2_EVENT="$(pwd)/run-auto.txt" git maintenance run --auto &&
	GIT_TRACE2_EVENT="$(pwd)/run-quiet.txt" git maintenance run --quiet &&
	grep ",\"gc\"]" run-no-auto.txt  &&
	grep ",\"gc\",\"--auto\"" run-auto.txt &&
	grep ",\"gc\",\"--quiet\"" run-quiet.txt
'

test_expect_success 'run --task=<task>' '
	GIT_TRACE2_EVENT="$(pwd)/run-commit-graph.txt" git maintenance run --task=commit-graph &&
	GIT_TRACE2_EVENT="$(pwd)/run-gc.txt" git maintenance run --task=gc &&
	GIT_TRACE2_EVENT="$(pwd)/run-commit-graph.txt" git maintenance run --task=commit-graph &&
	GIT_TRACE2_EVENT="$(pwd)/run-both.txt" git maintenance run --task=commit-graph --task=gc &&
	! grep ",\"gc\"" run-commit-graph.txt  &&
	grep ",\"gc\"" run-gc.txt  &&
	grep ",\"gc\"" run-both.txt  &&
	grep ",\"commit-graph\",\"write\"" run-commit-graph.txt  &&
	! grep ",\"commit-graph\",\"write\"" run-gc.txt  &&
	grep ",\"commit-graph\",\"write\"" run-both.txt
'

test_expect_success 'run --task=bogus' '
	test_must_fail git maintenance run --task=bogus 2>err &&
	test_i18ngrep "is not a valid task" err
'

test_expect_success 'run --task duplicate' '
	test_must_fail git maintenance run --task=gc --task=gc 2>err &&
	test_i18ngrep "cannot be selected multiple times" err
'

test_expect_success 'run --task=prefetch with no remotes' '
	git maintenance run --task=prefetch 2>err &&
	test_must_be_empty err
'

test_expect_success 'prefetch multiple remotes' '
	git clone . clone1 &&
	git clone . clone2 &&
	git remote add remote1 "file://$(pwd)/clone1" &&
	git remote add remote2 "file://$(pwd)/clone2" &&
	git -C clone1 switch -c one &&
	git -C clone2 switch -c two &&
	test_commit -C clone1 one &&
	test_commit -C clone2 two &&
	GIT_TRACE2_EVENT="$(pwd)/run-prefetch.txt" git maintenance run --task=prefetch &&
	grep ",\"fetch\",\"remote1\"" run-prefetch.txt &&
	grep ",\"fetch\",\"remote2\"" run-prefetch.txt &&
	test_path_is_missing .git/refs/remotes &&
	test_cmp clone1/.git/refs/heads/one .git/refs/prefetch/remote1/one &&
	test_cmp clone2/.git/refs/heads/two .git/refs/prefetch/remote2/two &&
	git log prefetch/remote1/one &&
	git log prefetch/remote2/two
'

test_done
