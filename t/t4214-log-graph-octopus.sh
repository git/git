#!/bin/sh

test_description='git log --graph of skewed left octopus merge.'

. ./test-lib.sh

test_expect_success 'set up merge history' '
	test_commit initial &&
	for i in 1 2 3 4 ; do
		git checkout master -b $i || return $?
		# Make tag name different from branch name, to avoid
		# ambiguity error when calling checkout.
		test_commit $i $i $i tag$i || return $?
	done &&
	git checkout 1 -b merge &&
	test_merge octopus-merge 1 2 3 4 &&
	test_commit after-merge &&
	git checkout 1 -b L &&
	test_commit left &&
	git checkout 4 -b crossover &&
	test_commit after-4 &&
	git checkout initial -b more-L &&
	test_commit after-initial
'

test_expect_success 'log --graph with tricky octopus merge, no color' '
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
	git log --color=never --graph --date-order --pretty=tformat:%s left octopus-merge >actual.raw &&
	sed "s/ *\$//" actual.raw >actual &&
	test_cmp expect.uncolored actual
'

test_expect_success 'log --graph with tricky octopus merge with colors' '
	test_config log.graphColors red,green,yellow,blue,magenta,cyan &&
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
	git log --color=always --graph --date-order --pretty=tformat:%s left octopus-merge >actual.colors.raw &&
	test_decode_color <actual.colors.raw | sed "s/ *\$//" >actual.colors &&
	test_cmp expect.colors actual.colors
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
	git log --color=never --graph --date-order --pretty=tformat:%s octopus-merge >actual.raw &&
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
	git log --color=always --graph --date-order --pretty=tformat:%s octopus-merge >actual.colors.raw &&
	test_decode_color <actual.colors.raw | sed "s/ *\$//" >actual.colors &&
	test_cmp expect.colors actual.colors
'

test_expect_success 'log --graph with normal octopus merge and child, no color' '
	cat >expect.uncolored <<-\EOF &&
	* after-merge
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
	git log --color=never --graph --date-order --pretty=tformat:%s after-merge >actual.raw &&
	sed "s/ *\$//" actual.raw >actual &&
	test_cmp expect.uncolored actual
'

test_expect_failure 'log --graph with normal octopus and child merge with colors' '
	cat >expect.colors <<-\EOF &&
	* after-merge
	*<BLUE>-<RESET><BLUE>-<RESET><MAGENTA>-<RESET><MAGENTA>.<RESET>   octopus-merge
	<GREEN>|<RESET><YELLOW>\<RESET> <BLUE>\<RESET> <MAGENTA>\<RESET>
	<GREEN>|<RESET> <YELLOW>|<RESET> <BLUE>|<RESET> * 4
	<GREEN>|<RESET> <YELLOW>|<RESET> * <MAGENTA>|<RESET> 3
	<GREEN>|<RESET> <YELLOW>|<RESET> <MAGENTA>|<RESET><MAGENTA>/<RESET>
	<GREEN>|<RESET> * <MAGENTA>|<RESET> 2
	<GREEN>|<RESET> <MAGENTA>|<RESET><MAGENTA>/<RESET>
	* <MAGENTA>|<RESET> 1
	<MAGENTA>|<RESET><MAGENTA>/<RESET>
	* initial
	EOF
	test_config log.graphColors red,green,yellow,blue,magenta,cyan &&
	git log --color=always --graph --date-order --pretty=tformat:%s after-merge >actual.colors.raw &&
	test_decode_color <actual.colors.raw | sed "s/ *\$//" >actual.colors &&
	test_cmp expect.colors actual.colors
'

test_expect_success 'log --graph with tricky octopus merge and its child, no color' '
	cat >expect.uncolored <<-\EOF &&
	* left
	| * after-merge
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
	git log --color=never --graph --date-order --pretty=tformat:%s left after-merge >actual.raw &&
	sed "s/ *\$//" actual.raw >actual &&
	test_cmp expect.uncolored actual
'

test_expect_failure 'log --graph with tricky octopus merge and its child with colors' '
	test_config log.graphColors red,green,yellow,blue,magenta,cyan &&
	cat >expect.colors <<-\EOF &&
	* left
	<RED>|<RESET> * after-merge
	<RED>|<RESET> *<MAGENTA>-<RESET><MAGENTA>-<RESET><CYAN>-<RESET><CYAN>.<RESET>   octopus-merge
	<RED>|<RESET> <RED>|<RESET><BLUE>\<RESET> <MAGENTA>\<RESET> <CYAN>\<RESET>
	<RED>|<RESET><RED>/<RESET> <BLUE>/<RESET> <MAGENTA>/<RESET> <CYAN>/<RESET>
	<RED>|<RESET> <BLUE>|<RESET> <MAGENTA>|<RESET> * 4
	<RED>|<RESET> <BLUE>|<RESET> * <CYAN>|<RESET> 3
	<RED>|<RESET> <BLUE>|<RESET> <CYAN>|<RESET><CYAN>/<RESET>
	<RED>|<RESET> * <CYAN>|<RESET> 2
	<RED>|<RESET> <CYAN>|<RESET><CYAN>/<RESET>
	* <CYAN>|<RESET> 1
	<CYAN>|<RESET><CYAN>/<RESET>
	* initial
	EOF
	git log --color=always --graph --date-order --pretty=tformat:%s left after-merge >actual.colors.raw &&
	test_decode_color <actual.colors.raw | sed "s/ *\$//" >actual.colors &&
	test_cmp expect.colors actual.colors
'

test_expect_success 'log --graph with crossover in octopus merge, no color' '
	cat >expect.uncolored <<-\EOF &&
	* after-4
	| *---.   octopus-merge
	| |\ \ \
	| |_|_|/
	|/| | |
	* | | | 4
	| | | * 3
	| |_|/
	|/| |
	| | * 2
	| |/
	|/|
	| * 1
	|/
	* initial
	EOF
	git log --color=never --graph --date-order --pretty=tformat:%s after-4 octopus-merge >actual.raw &&
	sed "s/ *\$//" actual.raw >actual &&
	test_cmp expect.uncolored actual
'

test_expect_failure 'log --graph with crossover in octopus merge with colors' '
	test_config log.graphColors red,green,yellow,blue,magenta,cyan &&
	cat >expect.colors <<-\EOF &&
	* after-4
	<RED>|<RESET> *<BLUE>-<RESET><BLUE>-<RESET><RED>-<RESET><RED>.<RESET>   octopus-merge
	<RED>|<RESET> <GREEN>|<RESET><YELLOW>\<RESET> <BLUE>\<RESET> <RED>\<RESET>
	<RED>|<RESET> <GREEN>|<RESET><RED>_<RESET><YELLOW>|<RESET><RED>_<RESET><BLUE>|<RESET><RED>/<RESET>
	<RED>|<RESET><RED>/<RESET><GREEN>|<RESET> <YELLOW>|<RESET> <BLUE>|<RESET>
	* <GREEN>|<RESET> <YELLOW>|<RESET> <BLUE>|<RESET> 4
	<MAGENTA>|<RESET> <GREEN>|<RESET> <YELLOW>|<RESET> * 3
	<MAGENTA>|<RESET> <GREEN>|<RESET><MAGENTA>_<RESET><YELLOW>|<RESET><MAGENTA>/<RESET>
	<MAGENTA>|<RESET><MAGENTA>/<RESET><GREEN>|<RESET> <YELLOW>|<RESET>
	<MAGENTA>|<RESET> <GREEN>|<RESET> * 2
	<MAGENTA>|<RESET> <GREEN>|<RESET><MAGENTA>/<RESET>
	<MAGENTA>|<RESET><MAGENTA>/<RESET><GREEN>|<RESET>
	<MAGENTA>|<RESET> * 1
	<MAGENTA>|<RESET><MAGENTA>/<RESET>
	* initial
	EOF
	git log --color=always --graph --date-order --pretty=tformat:%s after-4 octopus-merge >actual.colors.raw &&
	test_decode_color <actual.colors.raw | sed "s/ *\$//" >actual.colors &&
	test_cmp expect.colors actual.colors
'

test_expect_success 'log --graph with crossover in octopus merge and its child, no color' '
	cat >expect.uncolored <<-\EOF &&
	* after-4
	| * after-merge
	| *---.   octopus-merge
	| |\ \ \
	| |_|_|/
	|/| | |
	* | | | 4
	| | | * 3
	| |_|/
	|/| |
	| | * 2
	| |/
	|/|
	| * 1
	|/
	* initial
	EOF
	git log --color=never --graph --date-order --pretty=tformat:%s after-4 after-merge >actual.raw &&
	sed "s/ *\$//" actual.raw >actual &&
	test_cmp expect.uncolored actual
'

test_expect_failure 'log --graph with crossover in octopus merge and its child with colors' '
	test_config log.graphColors red,green,yellow,blue,magenta,cyan &&
	cat >expect.colors <<-\EOF &&
	* after-4
	<RED>|<RESET> * after-merge
	<RED>|<RESET> *<MAGENTA>-<RESET><MAGENTA>-<RESET><RED>-<RESET><RED>.<RESET>   octopus-merge
	<RED>|<RESET> <YELLOW>|<RESET><BLUE>\<RESET> <MAGENTA>\<RESET> <RED>\<RESET>
	<RED>|<RESET> <YELLOW>|<RESET><RED>_<RESET><BLUE>|<RESET><RED>_<RESET><MAGENTA>|<RESET><RED>/<RESET>
	<RED>|<RESET><RED>/<RESET><YELLOW>|<RESET> <BLUE>|<RESET> <MAGENTA>|<RESET>
	* <YELLOW>|<RESET> <BLUE>|<RESET> <MAGENTA>|<RESET> 4
	<CYAN>|<RESET> <YELLOW>|<RESET> <BLUE>|<RESET> * 3
	<CYAN>|<RESET> <YELLOW>|<RESET><CYAN>_<RESET><BLUE>|<RESET><CYAN>/<RESET>
	<CYAN>|<RESET><CYAN>/<RESET><YELLOW>|<RESET> <BLUE>|<RESET>
	<CYAN>|<RESET> <YELLOW>|<RESET> * 2
	<CYAN>|<RESET> <YELLOW>|<RESET><CYAN>/<RESET>
	<CYAN>|<RESET><CYAN>/<RESET><YELLOW>|<RESET>
	<CYAN>|<RESET> * 1
	<CYAN>|<RESET><CYAN>/<RESET>
	* initial
	EOF
	git log --color=always --graph --date-order --pretty=tformat:%s after-4 after-merge >actual.colors.raw &&
	test_decode_color <actual.colors.raw | sed "s/ *\$//" >actual.colors &&
	test_cmp expect.colors actual.colors
'

test_expect_success 'log --graph with unrelated commit and octopus tip, no color' '
	cat >expect.uncolored <<-\EOF &&
	* after-initial
	| *---.   octopus-merge
	| |\ \ \
	| | | | * 4
	| |_|_|/
	|/| | |
	| | | * 3
	| |_|/
	|/| |
	| | * 2
	| |/
	|/|
	| * 1
	|/
	* initial
	EOF
	git log --color=never --graph --date-order --pretty=tformat:%s after-initial octopus-merge >actual.raw &&
	sed "s/ *\$//" actual.raw >actual &&
	test_cmp expect.uncolored actual
'

test_expect_success 'log --graph with unrelated commit and octopus tip with colors' '
	test_config log.graphColors red,green,yellow,blue,magenta,cyan &&
	cat >expect.colors <<-\EOF &&
	* after-initial
	<RED>|<RESET> *<BLUE>-<RESET><BLUE>-<RESET><MAGENTA>-<RESET><MAGENTA>.<RESET>   octopus-merge
	<RED>|<RESET> <GREEN>|<RESET><YELLOW>\<RESET> <BLUE>\<RESET> <MAGENTA>\<RESET>
	<RED>|<RESET> <GREEN>|<RESET> <YELLOW>|<RESET> <BLUE>|<RESET> * 4
	<RED>|<RESET> <GREEN>|<RESET><RED>_<RESET><YELLOW>|<RESET><RED>_<RESET><BLUE>|<RESET><RED>/<RESET>
	<RED>|<RESET><RED>/<RESET><GREEN>|<RESET> <YELLOW>|<RESET> <BLUE>|<RESET>
	<RED>|<RESET> <GREEN>|<RESET> <YELLOW>|<RESET> * 3
	<RED>|<RESET> <GREEN>|<RESET><RED>_<RESET><YELLOW>|<RESET><RED>/<RESET>
	<RED>|<RESET><RED>/<RESET><GREEN>|<RESET> <YELLOW>|<RESET>
	<RED>|<RESET> <GREEN>|<RESET> * 2
	<RED>|<RESET> <GREEN>|<RESET><RED>/<RESET>
	<RED>|<RESET><RED>/<RESET><GREEN>|<RESET>
	<RED>|<RESET> * 1
	<RED>|<RESET><RED>/<RESET>
	* initial
	EOF
	git log --color=always --graph --date-order --pretty=tformat:%s after-initial octopus-merge >actual.colors.raw &&
	test_decode_color <actual.colors.raw | sed "s/ *\$//" >actual.colors &&
	test_cmp expect.colors actual.colors
'

test_expect_success 'log --graph with unrelated commit and octopus child, no color' '
	cat >expect.uncolored <<-\EOF &&
	* after-initial
	| * after-merge
	| *---.   octopus-merge
	| |\ \ \
	| | | | * 4
	| |_|_|/
	|/| | |
	| | | * 3
	| |_|/
	|/| |
	| | * 2
	| |/
	|/|
	| * 1
	|/
	* initial
	EOF
	git log --color=never --graph --date-order --pretty=tformat:%s after-initial after-merge >actual.raw &&
	sed "s/ *\$//" actual.raw >actual &&
	test_cmp expect.uncolored actual
'

test_expect_failure 'log --graph with unrelated commit and octopus child with colors' '
	test_config log.graphColors red,green,yellow,blue,magenta,cyan &&
	cat >expect.colors <<-\EOF &&
	* after-initial
	<RED>|<RESET> * after-merge
	<RED>|<RESET> *<MAGENTA>-<RESET><MAGENTA>-<RESET><CYAN>-<RESET><CYAN>.<RESET>   octopus-merge
	<RED>|<RESET> <YELLOW>|<RESET><BLUE>\<RESET> <MAGENTA>\<RESET> <CYAN>\<RESET>
	<RED>|<RESET> <YELLOW>|<RESET> <BLUE>|<RESET> <MAGENTA>|<RESET> * 4
	<RED>|<RESET> <YELLOW>|<RESET><RED>_<RESET><BLUE>|<RESET><RED>_<RESET><MAGENTA>|<RESET><RED>/<RESET>
	<RED>|<RESET><RED>/<RESET><YELLOW>|<RESET> <BLUE>|<RESET> <MAGENTA>|<RESET>
	<RED>|<RESET> <YELLOW>|<RESET> <BLUE>|<RESET> * 3
	<RED>|<RESET> <YELLOW>|<RESET><RED>_<RESET><BLUE>|<RESET><RED>/<RESET>
	<RED>|<RESET><RED>/<RESET><YELLOW>|<RESET> <BLUE>|<RESET>
	<RED>|<RESET> <YELLOW>|<RESET> * 2
	<RED>|<RESET> <YELLOW>|<RESET><RED>/<RESET>
	<RED>|<RESET><RED>/<RESET><YELLOW>|<RESET>
	<RED>|<RESET> * 1
	<RED>|<RESET><RED>/<RESET>
	* initial
	EOF
	git log --color=always --graph --date-order --pretty=tformat:%s after-initial after-merge >actual.colors.raw &&
	test_decode_color <actual.colors.raw | sed "s/ *\$//" >actual.colors &&
	test_cmp expect.colors actual.colors
'

test_done
