#!/bin/sh

test_description='Test wacky input to git config'
. ./test-lib.sh

setup() {
	(printf "[section]\n" &&
	printf "  key = foo") >.git/config
}

check() {
	echo "$2" >expected
	git config --get "$1" >actual 2>&1
	test_cmp actual expected
}

test_expect_success 'modify same key' '
	setup &&
	git config section.key bar &&
	check section.key bar
'

test_expect_success 'add key in same section' '
	setup &&
	git config section.other bar &&
	check section.key foo &&
	check section.other bar
'

test_expect_success 'add key in different section' '
	setup &&
	git config section2.key bar &&
	check section.key foo &&
	check section2.key bar
'

SECTION="test.q\"s\\sq'sp e.key"
test_expect_success 'make sure git config escapes section names properly' '
	git config "$SECTION" bar &&
	check "$SECTION" bar
'

LONG_VALUE=$(printf "x%01021dx a" 7)
test_expect_success 'do not crash on special long config line' '
	setup &&
	git config section.key "$LONG_VALUE" &&
	check section.key "$LONG_VALUE"
'

test_done
