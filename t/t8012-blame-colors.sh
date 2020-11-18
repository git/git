#!/bin/sh

test_description='colored git blame'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

PROG='git blame -c'
. "$TEST_DIRECTORY"/annotate-tests.sh

test_expect_success 'colored blame colors contiguous lines' '
	git -c color.blame.repeatedLines=yellow blame --color-lines --abbrev=12 hello.c >actual.raw &&
	git -c color.blame.repeatedLines=yellow -c blame.coloring=repeatedLines blame --abbrev=12 hello.c >actual.raw.2 &&
	test_cmp actual.raw actual.raw.2 &&
	test_decode_color <actual.raw >actual &&
	grep "<YELLOW>" <actual >darkened &&
	grep "(F" darkened > F.expect &&
	grep "(H" darkened > H.expect &&
	test_line_count = 2 F.expect &&
	test_line_count = 3 H.expect
'

test_expect_success 'color by age consistently colors old code' '
	git blame --color-by-age hello.c >actual.raw &&
	git -c blame.coloring=highlightRecent blame hello.c >actual.raw.2 &&
	test_cmp actual.raw actual.raw.2 &&
	test_decode_color <actual.raw >actual &&
	grep "<BLUE>" <actual >colored &&
	test_line_count = 10 colored
'

test_expect_success 'blame color by age: new code is different' '
	cat >>hello.c <<-EOF &&
		void qfunc();
	EOF
	git add hello.c &&
	GIT_AUTHOR_DATE="" git commit -m "new commit" &&

	git -c color.blame.highlightRecent="yellow,1 month ago, cyan" blame --color-by-age hello.c >actual.raw &&
	test_decode_color <actual.raw >actual &&

	grep "<YELLOW>" <actual >colored &&
	test_line_count = 10 colored &&

	grep "<CYAN>" <actual >colored &&
	test_line_count = 1 colored &&
	grep qfunc colored
'

test_done
