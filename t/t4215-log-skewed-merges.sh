#!/bin/sh

test_description='git log --graph of skewed merges'

. ./test-lib.sh

test_expect_success 'log --graph with merge fusing with its left and right neighbors' '
	cat >expect <<-\EOF &&
	*   H
	|\
	| *   G
	| |\
	| | * F
	| * \   E
	|/|\ \
	| | |/
	| | * D
	| * | C
	| |/
	* | B
	|/
	* A
	EOF

	git checkout --orphan _p &&
	test_commit A &&
	test_commit B &&
	git checkout -b _q @^ && test_commit C &&
	git checkout -b _r @^ && test_commit D &&
	git checkout _p && git merge --no-ff _q _r -m E &&
	git checkout _r && test_commit F &&
	git checkout _p && git merge --no-ff _r -m G &&
	git checkout @^^ && git merge --no-ff _p -m H &&

	git log --graph --pretty=tformat:%s | sed "s/ *$//" >actual &&
	test_cmp expect actual
'

test_expect_success 'log --graph with left-skewed merge' '
	cat >expect <<-\EOF &&
	*-----.   0_H
	|\ \ \ \
	| | | | * 0_G
	| |_|_|/|
	|/| | | |
	| | | * \   0_F
	| |_|/|\ \
	|/| | | |/
	| | | | * 0_E
	| |_|_|/
	|/| | |
	| | * | 0_D
	| | |/
	| | * 0_C
	| |/
	|/|
	| * 0_B
	|/
	* 0_A
	EOF

	git checkout --orphan 0_p && test_commit 0_A &&
	git checkout -b 0_q 0_p && test_commit 0_B &&
	git checkout -b 0_r 0_p &&
	test_commit 0_C &&
	test_commit 0_D &&
	git checkout -b 0_s 0_p && test_commit 0_E &&
	git checkout -b 0_t 0_p && git merge --no-ff 0_r^ 0_s -m 0_F &&
	git checkout 0_p && git merge --no-ff 0_s -m 0_G &&
	git checkout @^ && git merge --no-ff 0_q 0_r 0_t 0_p -m 0_H &&

	git log --graph --pretty=tformat:%s | sed "s/ *$//" >actual &&
	test_cmp expect actual
'

test_done
