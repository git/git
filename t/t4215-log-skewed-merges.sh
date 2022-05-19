#!/bin/sh

test_description='but log --graph of skewed merges'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-log-graph.sh

check_graph () {
	cat >expect &&
	lib_test_cmp_graph --format=%s "$@"
}

test_expect_success 'log --graph with merge fusing with its left and right neighbors' '
	but checkout --orphan _p &&
	test_cummit A &&
	test_cummit B &&
	but checkout -b _q @^ && test_cummit C &&
	but checkout -b _r @^ && test_cummit D &&
	but checkout _p && but merge --no-ff _q _r -m E &&
	but checkout _r && test_cummit F &&
	but checkout _p && but merge --no-ff _r -m G &&
	but checkout @^^ && but merge --no-ff _p -m H &&

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
	but checkout --orphan 0_p && test_cummit 0_A &&
	but checkout -b 0_q 0_p && test_cummit 0_B &&
	but checkout -b 0_r 0_p &&
	test_cummit 0_C &&
	test_cummit 0_D &&
	but checkout -b 0_s 0_p && test_cummit 0_E &&
	but checkout -b 0_t 0_p && but merge --no-ff 0_r^ 0_s -m 0_F &&
	but checkout 0_p && but merge --no-ff 0_s -m 0_G &&
	but checkout @^ && but merge --no-ff 0_q 0_r 0_t 0_p -m 0_H &&

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
	but checkout --orphan 1_p &&
	test_cummit 1_A &&
	test_cummit 1_B &&
	test_cummit 1_C &&
	but checkout -b 1_q @^ && test_cummit 1_D &&
	but checkout 1_p && but merge --no-ff 1_q -m 1_E &&
	but checkout -b 1_r @~3 && test_cummit 1_F &&
	but checkout 1_p && but merge --no-ff 1_r -m 1_G &&
	but checkout @^^ && but merge --no-ff 1_p -m 1_H &&

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
	but checkout --orphan 2_p &&
	test_cummit 2_A &&
	test_cummit 2_B &&
	test_cummit 2_C &&
	but checkout -b 2_q @^^ &&
	test_cummit 2_D &&
	test_cummit 2_E &&
	but checkout -b 2_r @^ && test_cummit 2_F &&
	but checkout 2_q &&
	but merge --no-ff 2_r -m 2_G &&
	but merge --no-ff 2_p^ -m 2_H &&
	but checkout -b 2_s @^^ && but merge --no-ff 2_q -m 2_J &&
	but checkout 2_p && but merge --no-ff 2_s -m 2_K &&

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
	but checkout --orphan 3_p &&
	test_cummit 3_A &&
	but checkout -b 3_q &&
	test_cummit 3_B &&
	test_cummit 3_C &&
	but checkout -b 3_r @^ &&
	test_cummit 3_D &&
	but checkout 3_q && but merge --no-ff 3_r -m 3_E &&
	but checkout 3_p && but merge --no-ff 3_q -m 3_F &&
	but checkout 3_r && test_cummit 3_G &&
	but checkout 3_p && but merge --no-ff 3_r -m 3_H &&
	but checkout @^^ && but merge --no-ff 3_p -m 3_J &&

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
	but checkout --orphan 4_p &&
	test_cummit 4_A &&
	test_cummit 4_B &&
	test_cummit 4_C &&
	but checkout -b 4_q @^^ && test_cummit 4_D &&
	but checkout -b 4_r 4_p^ && but merge --no-ff 4_q -m 4_E &&
	but checkout -b 4_s 4_p^^ &&
	but merge --no-ff 4_r -m 4_F &&
	but merge --no-ff 4_p -m 4_G &&
	but checkout @^^ && but merge --no-ff 4_s -m 4_H &&

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
	but checkout --orphan 5_p &&
	test_cummit 5_A &&
	but branch 5_q &&
	but branch 5_r &&
	test_cummit 5_B &&
	but checkout 5_q && test_cummit 5_C &&
	but checkout 5_r && test_cummit 5_D &&
	but checkout 5_p &&
	but merge --no-ff 5_q 5_r -m 5_E &&
	but checkout 5_q && test_cummit 5_F &&
	but checkout -b 5_s 5_p^ &&
	but merge --no-ff 5_p 5_q -m 5_G &&
	but checkout 5_r &&
	but merge --no-ff 5_s -m 5_H &&

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
	but checkout --orphan 6_1 &&
	test_cummit 6_A &&
	but branch 6_2 &&
	but branch 6_4 &&
	test_cummit 6_B &&
	but branch 6_3 &&
	test_cummit 6_C &&
	but checkout 6_2 && test_cummit 6_D &&
	but checkout 6_3 && test_cummit 6_E &&
	but checkout -b 6_5 6_1 &&
	but merge --no-ff 6_2 -m 6_F &&
	but checkout 6_4 && test_cummit 6_G &&
	but checkout 6_3 &&
	but merge --no-ff 6_4 -m 6_H &&
	but checkout 6_1 &&
	but merge --no-ff 6_2 -m 6_I &&

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
	but checkout --orphan 7_1 &&
	test_cummit 7_A &&
	test_cummit 7_B &&
	test_cummit 7_C &&
	but checkout -b 7_2 7_1~2 &&
	test_cummit 7_D &&
	test_cummit 7_E &&
	but checkout -b 7_3 7_1~1 &&
	test_cummit 7_F &&
	test_cummit 7_G &&
	but checkout -b 7_4 7_2~1 &&
	test_cummit 7_H &&
	but checkout -b 7_5 7_1~2 &&
	test_cummit 7_I &&
	but checkout -b 7_6 7_3~1 &&
	test_cummit 7_J &&
	but checkout -b M_1 7_1 &&
	but merge --no-ff 7_2 -m 7_M1 &&
	but checkout -b M_3 7_3 &&
	but merge --no-ff 7_4 -m 7_M2 &&
	but checkout -b M_5 7_5 &&
	but merge --no-ff 7_6 -m 7_M3 &&
	but checkout -b M_7 7_1 &&
	but merge --no-ff 7_2 7_3 -m 7_M4 &&

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
