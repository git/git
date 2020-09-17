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

test_expect_success 'run [--auto|--quiet]' '
	GIT_TRACE2_EVENT="$(pwd)/run-no-auto.txt" \
		git maintenance run 2>/dev/null &&
	GIT_TRACE2_EVENT="$(pwd)/run-auto.txt" \
		git maintenance run --auto 2>/dev/null &&
	GIT_TRACE2_EVENT="$(pwd)/run-no-quiet.txt" \
		git maintenance run --no-quiet 2>/dev/null &&
	test_subcommand git gc --quiet <run-no-auto.txt &&
	test_subcommand git gc --auto --quiet <run-auto.txt &&
	test_subcommand git gc --no-quiet <run-no-quiet.txt
'

test_done
