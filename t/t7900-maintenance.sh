#!/bin/sh

test_description='git maintenance builtin'

GIT_TEST_COMMIT_GRAPH=0
GIT_TEST_MULTI_PACK_INDEX=0

. ./test-lib.sh

test_expect_success 'help text' '
	test_must_fail git maintenance -h 2>err &&
	test_i18ngrep "usage: git maintenance run" err
'

test_expect_success 'gc [--auto]' '
	GIT_TRACE2_EVENT="$(pwd)/run-no-auto.txt" git maintenance run &&
	GIT_TRACE2_EVENT="$(pwd)/run-auto.txt" git maintenance run --auto &&
	grep ",\"gc\"]" run-no-auto.txt  &&
	grep ",\"gc\",\"--auto\"]" run-auto.txt
'

test_done
