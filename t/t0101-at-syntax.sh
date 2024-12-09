#!/bin/sh

test_description='various @{whatever} syntax tests'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit one &&
	test_commit two
'

check_at() {
	echo "$2" >expect &&
	git log -1 --format=%s "$1" >actual &&
	test_cmp expect actual
}

test_expect_success '@{0} shows current' '
	check_at @{0} two
'

test_expect_success '@{1} shows old' '
	check_at @{1} one
'

test_expect_success '@{now} shows current' '
	check_at @{now} two
'

test_expect_success '@{2001-09-17} (before the first commit) shows old' '
	check_at @{2001-09-17} one
'

test_expect_success 'silly approxidates work' '
	check_at @{3.hot.dogs.on.2001-09-17} one
'

test_expect_success 'notice misspelled upstream' '
	test_must_fail git log -1 --format=%s @{usptream}
'

test_expect_success 'complain about total nonsense' '
	test_must_fail git log -1 --format=%s @{utter.bogosity}
'

test_done
