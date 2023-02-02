#!/bin/sh

test_description='colored git blame'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_CREATE_REPO_NO_TEMPLATE=1
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

# shellcheck disable=SC2317
re_normalize_color_decoded_blame() {
	# Construct the regex used by `normalize_color_decoded_blame`.
	# This is simply for documentation: line comments aren't permitted in
	# backslash-continuation lines, and POSIX sh does't support 'x+=' syntax.
	printf '%s' '\(<[^>]*>\)'     # 1: capture the "<YELLOW>" etc
	printf '%s' ' *'              # -- discard any spaces
	printf '%s' '[0-9a-f]\{1,\}'  # -- discard the commit ID
	printf '%s' ' *'              # -- discard any spaces
	printf '%s' '('               # -- left paren
	printf '%s' '\(.\)'           # 2: capture the single-char A/F/G/etc
	printf '%s' '[^\)]*'          # -- discard author and timestamp stuff
	printf '%s' '\([0-9]\)\{1,\}' # 3: capture the line number
	printf '%s' ')'               # -- right paren
	printf '%s' ' *'              # -- discard leading spaces
	printf '%s' '\(.*$\)'         # 4: capture the remainder
}

# shellcheck disable=SC2317
normalize_color_decoded_blame() {
	# Reads from stdin and writes to stdout.
	# Removes the commit ID and author/timestamp from blame output after
	# "test_decode_color" has run.
	#
	# This is simply to make expected  outputs easier to describe
	# and compare without having to refer to magic line counts.
	re="$(re_normalize_color_decoded_blame)" || return $?
	sed -e 's/^'"${re}"'/\1 (\2 \3) \4/'
}

test_expect_success 'blame color by age and lines' '
	git \
		-c color.blame.repeatedLines=blue \
		-c color.blame.highlightRecent="yellow,1 month ago, cyan" \
		blame \
		--color-lines \
		--color-by-age \
		hello.c \
		>actual.raw &&

	git \
		-c color.blame.repeatedLines=blue \
		-c color.blame.highlightRecent="yellow,1 month ago, cyan" \
		-c blame.coloring=highlightRecent,repeatedLines \
		blame hello.c \
		>actual.raw.2 &&
	test_cmp actual.raw actual.raw.2 &&

	test_decode_color <actual.raw >actual &&
	normalize_color_decoded_blame <actual >actual.norm &&

	normalize_color_decoded_blame >expected.norm <<-EOF &&
		<YELLOW>11111111 (H ... 1)  <RESET>#include <stdio.h>
		<YELLOW>22222222 (F ... 2)  <RESET>int main(int argc, const char *argv[])
		<BLUE>  22222222 (F ... 3)  <RESET>{
		<BLUE>  22222222 (F ... 4)  <RESET>	puts("hello");
		<YELLOW>33333333 (G ... 5)  <RESET>	puts("goodbye");
		<YELLOW>22222222 (F ... 6)  <RESET>}
		<YELLOW>11111111 (H ... 7)  <RESET>void mail()
		<BLUE>  11111111 (H ... 8)  <RESET>{
		<BLUE>  11111111 (H ... 9)  <RESET>	puts("mail");
		<BLUE>  11111111 (H ... 10) <RESET>}
		<CYAN>  44444444 (A ... 11) <RESET>void qfunc();
		EOF

	test_cmp actual.norm expected.norm &&

	grep "<YELLOW>" <actual.norm >sanity-check &&
	test_line_count = 5 sanity-check
'

test_done
