#!/bin/sh

test_description='git maintenance builtin'

. ./test-lib.sh

test_expect_success 'help text' '
	test_expect_code 129 git maintenance -h 2>err &&
	test_i18ngrep "usage: git maintenance run" err &&
	test_expect_code 128 git maintenance barf 2>err &&
	test_i18ngrep "invalid subcommand: barf" err &&
	test_expect_code 129 git maintenance 2>err &&
	test_i18ngrep "usage: git maintenance" err
'

test_expect_success 'run [--auto]' '
	GIT_TRACE2_EVENT="$(pwd)/run-no-auto.txt" git maintenance run &&
	GIT_TRACE2_EVENT="$(pwd)/run-auto.txt" git maintenance run --auto &&
	test_subcommand git gc <run-no-auto.txt &&
	test_subcommand git gc --auto <run-auto.txt
'

test_done
