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
	| | |
	| |  \
	| *-. \   E
	| |\ \ \
	|/ / / /
	| | | /
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

test_done
