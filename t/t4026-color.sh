#!/bin/sh
#
# Copyright (c) 2008 Timo Hirvonen
#

test_description='Test diff/status color escape codes'

. ./test-lib.sh

ESC=$(printf '\033')
color()
{
	actual=$(git config --get-color no.such.slot "$1") &&
	test "$actual" = "${2:+$ESC}$2"
}

invalid_color()
{
	test_must_fail git config --get-color no.such.slot "$1"
}

test_expect_success 'reset' '
	color "reset" "[m"
'

test_expect_success 'empty color is empty' '
	color "" ""
'

test_expect_success 'attribute before color name' '
	color "bold red" "[1;31m"
'

test_expect_success 'aixterm bright fg color' '
	color "brightred" "[91m"
'

test_expect_success 'aixterm bright bg color' '
	color "green brightblue" "[32;104m"
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

test_expect_success 'fg bg attr...' '
	color "blue bold dim ul blink reverse" "[1;2;4;5;7;34m"
'

test_expect_success 'reset fg bg attr...' '
	color "reset blue bold dim ul blink reverse" "[;1;2;4;5;7;34m"
'

# note that nobold and nodim are the same code (22)
test_expect_success 'attr negation' '
	color "nobold nodim noul noblink noreverse" "[22;24;25;27m"
'

test_expect_success '"no-" variant of negation' '
	color "no-bold no-blink" "[22;25m"
'

test_expect_success 'long color specification' '
	color "254 255 bold dim ul blink reverse" "[1;2;4;5;7;38;5;254;48;5;255m"
'

test_expect_success 'absurdly long color specification' '
	color \
	  "#ffffff #ffffff bold nobold dim nodim italic noitalic
	   ul noul blink noblink reverse noreverse strike nostrike" \
	  "[1;2;3;4;5;7;9;22;23;24;25;27;29;38;2;255;255;255;48;2;255;255;255m"
'

test_expect_success '0-7 are aliases for basic ANSI color names' '
	color "0 7" "[30;47m"
'

test_expect_success '8-15 are aliases for aixterm color names' '
	color "12 13" "[94;105m"
'

test_expect_success '256 colors' '
	color "254 bold 255" "[1;38;5;254;48;5;255m"
'

test_expect_success 'RGB colors' '
	color "#ff00ff #0f0" "[38;2;255;0;255;48;2;0;255;0m"
'

test_expect_success '"default" foreground' '
	color "default" "[39m"
'

test_expect_success '"normal default" to clear background' '
	color "normal default" "[49m"
'

test_expect_success '"default" can be combined with attributes' '
	color "default default no-reverse bold" "[1;27;39;49m"
'

test_expect_success '"normal" yields no color at all' '
	color "normal black" "[40m"
'

test_expect_success '-1 is a synonym for "normal"' '
	color "-1 black" "[40m"
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

test_expect_success 'non-hex character in RGB color' '
	invalid_color "#x23456" &&
	invalid_color "#1x3456" &&
	invalid_color "#12x456" &&
	invalid_color "#123x56" &&
	invalid_color "#1234x6" &&
	invalid_color "#12345x" &&
	invalid_color "#x23" &&
	invalid_color "#1x3" &&
	invalid_color "#12x"
'

test_expect_success 'wrong number of letters in RGB color' '
	invalid_color "#1" &&
	invalid_color "#23" &&
	invalid_color "#789a" &&
	invalid_color "#bcdef" &&
	invalid_color "#1234567"
'

test_expect_success 'unknown color slots are ignored (diff)' '
	git config color.diff.nosuchslotwilleverbedefined white &&
	git diff --color
'

test_expect_success 'unknown color slots are ignored (branch)' '
	git config color.branch.nosuchslotwilleverbedefined white &&
	git branch -a
'

test_expect_success 'unknown color slots are ignored (status)' '
	git config color.status.nosuchslotwilleverbedefined white &&
	{ git status; ret=$?; } &&
	case $ret in 0|1) : ok ;; *) false ;; esac
'

test_done
