#!/bin/sh

test_description='Test wacky input to git config'
. ./test-lib.sh

setup() {
	(printf "[section]\n" &&
	printf "  key = foo") >.git/config
}

check() {
	echo "$2" >expected
	git config --get "$1" >actual
	git diff actual expected
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

test_done
