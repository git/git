#!/bin/sh

test_description='git log --graph of skewed merges'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-log-graph.sh

check_graph () {
	cat >expect &&
	lib_test_cmp_graph --format=%s "$@"
}

test_expect_success 'log --graph with merge fusing with its left and right neighbors' '
	git checkout --orphan _p &&
	test_commit A &&
	test_commit B &&
	git checkout -b _q @^ && test_commit C &&
	git checkout -b _r @^ && test_commit D &&
	git checkout _p && git merge --no-ff _q _r -m E &&
	git checkout _r && test_commit F &&
	git checkout _p && git merge --no-ff _r -m G &&
	git checkout @^^ && git merge --no-ff _p -m H &&

	check_graph <<-\EOF
	*   H
	|\
	| *   G
	| |\
	| | * F
	| * | E
	|/|\|
	| | * D
	| * | C
	| |/
	* / B
	|/
	* A
	EOF
'

test_expect_success 'log --graph with left-skewed merge' '
	git checkout --orphan 0_p && test_commit 0_A &&
	git checkout -b 0_q 0_p && test_commit 0_B &&
	git checkout -b 0_r 0_p &&
	test_commit 0_C &&
	test_commit 0_D &&
	git checkout -b 0_s 0_p && test_commit 0_E &&
	git checkout -b 0_t 0_p && git merge --no-ff 0_r^ 0_s -m 0_F &&
	git checkout 0_p && git merge --no-ff 0_s -m 0_G &&
	git checkout @^ && git merge --no-ff 0_q 0_r 0_t 0_p -m 0_H &&

	check_graph <<-\EOF
	*-----.   0_H
	|\ \ \ \
	| | | | * 0_G
	| |_|_|/|
	|/| | | |
	| | | * | 0_F
	| |_|/|\|
	|/| | | |
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
'

test_expect_success 'log --graph with nested left-skewed merge' '
	git checkout --orphan 1_p &&
	test_commit 1_A &&
	test_commit 1_B &&
	test_commit 1_C &&
	git checkout -b 1_q @^ && test_commit 1_D &&
	git checkout 1_p && git merge --no-ff 1_q -m 1_E &&
	git checkout -b 1_r @~3 && test_commit 1_F &&
	git checkout 1_p && git merge --no-ff 1_r -m 1_G &&
	git checkout @^^ && git merge --no-ff 1_p -m 1_H &&

	check_graph <<-\EOF
	*   1_H
	|\
	| *   1_G
	| |\
	| | * 1_F
	| * | 1_E
	|/| |
	| * | 1_D
	* | | 1_C
	|/ /
	* / 1_B
	|/
	* 1_A
	EOF
'

test_expect_success 'log --graph with nested left-skewed merge following normal merge' '
	git checkout --orphan 2_p &&
	test_commit 2_A &&
	test_commit 2_B &&
	test_commit 2_C &&
	git checkout -b 2_q @^^ &&
	test_commit 2_D &&
	test_commit 2_E &&
	git checkout -b 2_r @^ && test_commit 2_F &&
	git checkout 2_q &&
	git merge --no-ff 2_r -m 2_G &&
	git merge --no-ff 2_p^ -m 2_H &&
	git checkout -b 2_s @^^ && git merge --no-ff 2_q -m 2_J &&
	git checkout 2_p && git merge --no-ff 2_s -m 2_K &&

	check_graph <<-\EOF
	*   2_K
	|\
	| *   2_J
	| |\
	| | *   2_H
	| | |\
	| | * | 2_G
	| |/| |
	| | * | 2_F
	| * | | 2_E
	| |/ /
	| * | 2_D
	* | | 2_C
	| |/
	|/|
	* | 2_B
	|/
	* 2_A
	EOF
'

test_expect_success 'log --graph with nested right-skewed merge following left-skewed merge' '
	git checkout --orphan 3_p &&
	test_commit 3_A &&
	git checkout -b 3_q &&
	test_commit 3_B &&
	test_commit 3_C &&
	git checkout -b 3_r @^ &&
	test_commit 3_D &&
	git checkout 3_q && git merge --no-ff 3_r -m 3_E &&
	git checkout 3_p && git merge --no-ff 3_q -m 3_F &&
	git checkout 3_r && test_commit 3_G &&
	git checkout 3_p && git merge --no-ff 3_r -m 3_H &&
	git checkout @^^ && git merge --no-ff 3_p -m 3_J &&

	check_graph <<-\EOF
	*   3_J
	|\
	| *   3_H
	| |\
	| | * 3_G
	| * | 3_F
	|/| |
	| * | 3_E
	| |\|
	| | * 3_D
	| * | 3_C
	| |/
	| * 3_B
	|/
	* 3_A
	EOF
'

test_expect_success 'log --graph with right-skewed merge following a left-skewed one' '
	git checkout --orphan 4_p &&
	test_commit 4_A &&
	test_commit 4_B &&
	test_commit 4_C &&
	git checkout -b 4_q @^^ && test_commit 4_D &&
	git checkout -b 4_r 4_p^ && git merge --no-ff 4_q -m 4_E &&
	git checkout -b 4_s 4_p^^ &&
	git merge --no-ff 4_r -m 4_F &&
	git merge --no-ff 4_p -m 4_G &&
	git checkout @^^ && git merge --no-ff 4_s -m 4_H &&

	check_graph --date-order <<-\EOF
	*   4_H
	|\
	| *   4_G
	| |\
	| * | 4_F
	|/| |
	| * |   4_E
	| |\ \
	| | * | 4_D
	| |/ /
	|/| |
	| | * 4_C
	| |/
	| * 4_B
	|/
	* 4_A
	EOF
'

test_expect_success 'log --graph with octopus merge with column joining its penultimate parent' '
	git checkout --orphan 5_p &&
	test_commit 5_A &&
	git branch 5_q &&
	git branch 5_r &&
	test_commit 5_B &&
	git checkout 5_q && test_commit 5_C &&
	git checkout 5_r && test_commit 5_D &&
	git checkout 5_p &&
	git merge --no-ff 5_q 5_r -m 5_E &&
	git checkout 5_q && test_commit 5_F &&
	git checkout -b 5_s 5_p^ &&
	git merge --no-ff 5_p 5_q -m 5_G &&
	git checkout 5_r &&
	git merge --no-ff 5_s -m 5_H &&

	check_graph <<-\EOF
	*   5_H
	|\
	| *-.   5_G
	| |\ \
	| | | * 5_F
	| | * |   5_E
	| |/|\ \
	| |_|/ /
	|/| | /
	| | |/
	* | | 5_D
	| | * 5_C
	| |/
	|/|
	| * 5_B
	|/
	* 5_A
	EOF
'

test_expect_success 'log --graph with multiple tips' '
	git checkout --orphan 6_1 &&
	test_commit 6_A &&
	git branch 6_2 &&
	git branch 6_4 &&
	test_commit 6_B &&
	git branch 6_3 &&
	test_commit 6_C &&
	git checkout 6_2 && test_commit 6_D &&
	git checkout 6_3 && test_commit 6_E &&
	git checkout -b 6_5 6_1 &&
	git merge --no-ff 6_2 -m 6_F &&
	git checkout 6_4 && test_commit 6_G &&
	git checkout 6_3 &&
	git merge --no-ff 6_4 -m 6_H &&
	git checkout 6_1 &&
	git merge --no-ff 6_2 -m 6_I &&

	check_graph 6_1 6_3 6_5 <<-\EOF
	*   6_I
	|\
	| | *   6_H
	| | |\
	| | | * 6_G
	| | * | 6_E
	| | | | * 6_F
	| |_|_|/|
	|/| | |/
	| | |/|
	| |/| |
	| * | | 6_D
	| | |/
	| |/|
	* | | 6_C
	| |/
	|/|
	* | 6_B
	|/
	* 6_A
	EOF
'

test_expect_success 'log --graph with multiple tips and colors' '
	test_config log.graphColors red,green,yellow,blue,magenta,cyan &&
	cat >expect.colors <<-\EOF &&
	*   6_I
	<RED>|<RESET><GREEN>\<RESET>
	<RED>|<RESET> <GREEN>|<RESET> *   6_H
	<RED>|<RESET> <GREEN>|<RESET> <YELLOW>|<RESET><BLUE>\<RESET>
	<RED>|<RESET> <GREEN>|<RESET> <YELLOW>|<RESET> * 6_G
	<RED>|<RESET> <GREEN>|<RESET> <YELLOW>|<RESET> <BLUE>|<RESET> * 6_F
	<RED>|<RESET> <GREEN>|<RESET><RED>_<RESET><YELLOW>|<RESET><RED>_<RESET><BLUE>|<RESET><RED>/<RESET><GREEN>|<RESET>
	<RED>|<RESET><RED>/<RESET><GREEN>|<RESET> <YELLOW>|<RESET> <BLUE>|<RESET><GREEN>/<RESET>
	<RED>|<RESET> <GREEN>|<RESET> <YELLOW>|<RESET><GREEN>/<RESET><BLUE>|<RESET>
	<RED>|<RESET> <GREEN>|<RESET><GREEN>/<RESET><YELLOW>|<RESET> <BLUE>|<RESET>
	<RED>|<RESET> <GREEN>|<RESET> * <BLUE>|<RESET> 6_E
	<RED>|<RESET> * <CYAN>|<RESET> <BLUE>|<RESET> 6_D
	<RED>|<RESET> <BLUE>|<RESET> <CYAN>|<RESET><BLUE>/<RESET>
	<RED>|<RESET> <BLUE>|<RESET><BLUE>/<RESET><CYAN>|<RESET>
	* <BLUE>|<RESET> <CYAN>|<RESET> 6_C
	<CYAN>|<RESET> <BLUE>|<RESET><CYAN>/<RESET>
	<CYAN>|<RESET><CYAN>/<RESET><BLUE>|<RESET>
	* <BLUE>|<RESET> 6_B
	<BLUE>|<RESET><BLUE>/<RESET>
	* 6_A
	EOF
	lib_test_cmp_colored_graph --date-order --pretty=tformat:%s 6_1 6_3 6_5
'

test_expect_success 'log --graph with multiple tips' '
	git checkout --orphan 7_1 &&
	test_commit 7_A &&
	test_commit 7_B &&
	test_commit 7_C &&
	git checkout -b 7_2 7_1~2 &&
	test_commit 7_D &&
	test_commit 7_E &&
	git checkout -b 7_3 7_1~1 &&
	test_commit 7_F &&
	test_commit 7_G &&
	git checkout -b 7_4 7_2~1 &&
	test_commit 7_H &&
	git checkout -b 7_5 7_1~2 &&
	test_commit 7_I &&
	git checkout -b 7_6 7_3~1 &&
	test_commit 7_J &&
	git checkout -b M_1 7_1 &&
	git merge --no-ff 7_2 -m 7_M1 &&
	git checkout -b M_3 7_3 &&
	git merge --no-ff 7_4 -m 7_M2 &&
	git checkout -b M_5 7_5 &&
	git merge --no-ff 7_6 -m 7_M3 &&
	git checkout -b M_7 7_1 &&
	git merge --no-ff 7_2 7_3 -m 7_M4 &&

	check_graph M_1 M_3 M_5 M_7 <<-\EOF
	*   7_M1
	|\
	| | *   7_M2
	| | |\
	| | | * 7_H
	| | | | *   7_M3
	| | | | |\
	| | | | | * 7_J
	| | | | * | 7_I
	| | | | | | *   7_M4
	| |_|_|_|_|/|\
	|/| | | | |/ /
	| | |_|_|/| /
	| |/| | | |/
	| | | |_|/|
	| | |/| | |
	| | * | | | 7_G
	| | | |_|/
	| | |/| |
	| | * | | 7_F
	| * | | | 7_E
	| | |/ /
	| |/| |
	| * | | 7_D
	| | |/
	| |/|
	* | | 7_C
	| |/
	|/|
	* | 7_B
	|/
	* 7_A
	EOF
'

test_done
