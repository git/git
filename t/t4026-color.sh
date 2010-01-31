#!/bin/sh
#
# Copyright (c) 2008 Timo Hirvonen
#

test_description='Test diff/status color escape codes'
. ./test-lib.sh

color()
{
	git config diff.color.new "$1" &&
	test "`git config --get-color diff.color.new`" = "$2"
}

invalid_color()
{
	git config diff.color.new "$1" &&
	test -z "`git config --get-color diff.color.new 2>/dev/null`"
}

test_expect_success 'reset' '
	color "reset" "[m"
'

test_expect_success 'attribute before color name' '
	color "bold red" "[1;31m"
'

test_expect_success 'color name before attribute' '
	color "red bold" "[1;31m"
'

test_expect_success 'attr fg bg' '
	color "ul blue red" "[4;34;41m"
'

test_expect_success 'fg attr bg' '
	color "blue ul red" "[4;34;41m"
'

test_expect_success 'fg bg attr' '
	color "blue red ul" "[4;34;41m"
'

test_expect_success '256 colors' '
	color "254 bold 255" "[1;38;5;254;48;5;255m"
'

test_expect_success 'color too small' '
	invalid_color "-2"
'

test_expect_success 'color too big' '
	invalid_color "256"
'

test_expect_success 'extra character after color number' '
	invalid_color "3X"
'

test_expect_success 'extra character after color name' '
	invalid_color "redX"
'

test_expect_success 'extra character after attribute' '
	invalid_color "dimX"
'

test_done
