#!/bin/sh

test_description='Test wacky input to but config'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# Leaving off the newline is intentional!
setup() {
	(printf "[section]\n" &&
	printf "  key = foo") >.but/config
}

# 'check section.key value' verifies that the entry for section.key is
# 'value'
check() {
	echo "$2" >expected
	but config --get "$1" >actual 2>&1
	test_cmp expected actual
}

# 'check section.key regex value' verifies that the entry for
# section.key *that matches 'regex'* is 'value'
check_regex() {
	echo "$3" >expected
	but config --get "$1" "$2" >actual 2>&1
	test_cmp expected actual
}

test_expect_success 'modify same key' '
	setup &&
	but config section.key bar &&
	check section.key bar
'

test_expect_success 'add key in same section' '
	setup &&
	but config section.other bar &&
	check section.key foo &&
	check section.other bar
'

test_expect_success 'add key in different section' '
	setup &&
	but config section2.key bar &&
	check section.key foo &&
	check section2.key bar
'

SECTION="test.q\"s\\sq'sp e.key"
test_expect_success 'make sure but config escapes section names properly' '
	but config "$SECTION" bar &&
	check "$SECTION" bar
'

LONG_VALUE=$(printf "x%01021dx a" 7)
test_expect_success 'do not crash on special long config line' '
	setup &&
	but config section.key "$LONG_VALUE" &&
	check section.key "$LONG_VALUE"
'

setup_many() {
	setup &&
	# This time we want the newline so that we can tack on more
	# entries.
	echo >>.but/config &&
	# Semi-efficient way of concatenating 5^5 = 3125 lines. Note
	# that because 'setup' already put one line, this means 3126
	# entries for section.key in the config file.
	cat >5to1 <<-\EOF &&
	  key = foo
	  key = foo
	  key = foo
	  key = foo
	  key = foo
	EOF
	cat 5to1 5to1 5to1 5to1 5to1 >5to2 &&	   # 25
	cat 5to2 5to2 5to2 5to2 5to2 >5to3 &&	   # 125
	cat 5to3 5to3 5to3 5to3 5to3 >5to4 &&	   # 635
	cat 5to4 5to4 5to4 5to4 5to4 >>.but/config # 3125
}

test_expect_success 'get many entries' '
	setup_many &&
	but config --get-all section.key >actual &&
	test_line_count = 3126 actual
'

test_expect_success 'get many entries by regex' '
	setup_many &&
	but config --get-regexp "sec.*ke." >actual &&
	test_line_count = 3126 actual
'

test_expect_success 'add and replace one of many entries' '
	setup_many &&
	but config --add section.key bar &&
	check_regex section.key "b.*r" bar &&
	but config section.key beer "b.*r" &&
	check_regex section.key "b.*r" beer
'

test_expect_success 'replace many entries' '
	setup_many &&
	but config --replace-all section.key bar &&
	check section.key bar
'

test_expect_success 'unset many entries' '
	setup_many &&
	but config --unset-all section.key &&
	test_must_fail but config section.key
'

test_expect_success '--add appends new value after existing empty value' '
	cat >expect <<-\EOF &&


	fool
	roll
	EOF
	cp .but/config .but/config.old &&
	test_when_finished "mv .but/config.old .but/config" &&
	cat >.but/config <<-\EOF &&
	[foo]
		baz
		baz =
		baz = fool
	EOF
	but config --add foo.baz roll &&
	but config --get-all foo.baz >output &&
	test_cmp expect output
'

test_done
