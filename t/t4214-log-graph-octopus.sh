#!/bin/sh

test_description='git log --graph of skewed left octopus merge.'

. ./test-lib.sh

test_expect_success 'set up merge history' '
	cat >expect.uncolored <<-\EOF &&
	* left
	| *---.   octopus-merge
	| |\ \ \
	|/ / / /
	| | | * 4
	| | * | 3
	| | |/
	| * | 2
	| |/
	* | 1
	|/
	* initial
	EOF
	cat >expect.colors <<-\EOF &&
	* left
	<RED>|<RESET> *<BLUE>-<RESET><BLUE>-<RESET><MAGENTA>-<RESET><MAGENTA>.<RESET>   octopus-merge
	<RED>|<RESET> <RED>|<RESET><YELLOW>\<RESET> <BLUE>\<RESET> <MAGENTA>\<RESET>
	<RED>|<RESET><RED>/<RESET> <YELLOW>/<RESET> <BLUE>/<RESET> <MAGENTA>/<RESET>
	<RED>|<RESET> <YELLOW>|<RESET> <BLUE>|<RESET> * 4
	<RED>|<RESET> <YELLOW>|<RESET> * <MAGENTA>|<RESET> 3
	<RED>|<RESET> <YELLOW>|<RESET> <MAGENTA>|<RESET><MAGENTA>/<RESET>
	<RED>|<RESET> * <MAGENTA>|<RESET> 2
	<RED>|<RESET> <MAGENTA>|<RESET><MAGENTA>/<RESET>
	* <MAGENTA>|<RESET> 1
	<MAGENTA>|<RESET><MAGENTA>/<RESET>
	* initial
	EOF
	test_commit initial &&
	for i in 1 2 3 4 ; do
		git checkout master -b $i || return $?
		# Make tag name different from branch name, to avoid
		# ambiguity error when calling checkout.
		test_commit $i $i $i tag$i || return $?
	done &&
	git checkout 1 -b merge &&
	test_tick &&
	git merge -m octopus-merge 1 2 3 4 &&
	git checkout 1 -b L &&
	test_commit left
'

test_expect_success 'log --graph with tricky octopus merge with colors' '
	test_config log.graphColors red,green,yellow,blue,magenta,cyan &&
	git log --color=always --graph --date-order --pretty=tformat:%s --all >actual.colors.raw &&
	test_decode_color <actual.colors.raw | sed "s/ *\$//" >actual.colors &&
	test_cmp expect.colors actual.colors
'

test_expect_success 'log --graph with tricky octopus merge, no color' '
	git log --color=never --graph --date-order --pretty=tformat:%s --all >actual.raw &&
	sed "s/ *\$//" actual.raw >actual &&
	test_cmp expect.uncolored actual
'

# Repeat the previous two tests with "normal" octopus merge (i.e.,
# without the first parent skewing to the "left" branch column).

test_expect_success 'log --graph with normal octopus merge, no color' '
	cat >expect.uncolored <<-\EOF &&
	*---.   octopus-merge
	|\ \ \
	| | | * 4
	| | * | 3
	| | |/
	| * | 2
	| |/
	* | 1
	|/
	* initial
	EOF
	git log --color=never --graph --date-order --pretty=tformat:%s merge >actual.raw &&
	sed "s/ *\$//" actual.raw >actual &&
	test_cmp expect.uncolored actual
'

test_expect_success 'log --graph with normal octopus merge with colors' '
	cat >expect.colors <<-\EOF &&
	*<YELLOW>-<RESET><YELLOW>-<RESET><BLUE>-<RESET><BLUE>.<RESET>   octopus-merge
	<RED>|<RESET><GREEN>\<RESET> <YELLOW>\<RESET> <BLUE>\<RESET>
	<RED>|<RESET> <GREEN>|<RESET> <YELLOW>|<RESET> * 4
	<RED>|<RESET> <GREEN>|<RESET> * <BLUE>|<RESET> 3
	<RED>|<RESET> <GREEN>|<RESET> <BLUE>|<RESET><BLUE>/<RESET>
	<RED>|<RESET> * <BLUE>|<RESET> 2
	<RED>|<RESET> <BLUE>|<RESET><BLUE>/<RESET>
	* <BLUE>|<RESET> 1
	<BLUE>|<RESET><BLUE>/<RESET>
	* initial
	EOF
	test_config log.graphColors red,green,yellow,blue,magenta,cyan &&
	git log --color=always --graph --date-order --pretty=tformat:%s merge >actual.colors.raw &&
	test_decode_color <actual.colors.raw | sed "s/ *\$//" >actual.colors &&
	test_cmp expect.colors actual.colors
'
test_done
