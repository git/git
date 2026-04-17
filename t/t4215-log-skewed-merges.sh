#!/bin/sh

test_description='git log --graph of skewed merges'

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

test_expect_success 'log --graph --graph-lane-limit=2 limited to two lanes' '
	check_graph --graph-lane-limit=2 M_7 <<-\EOF
	*-.   7_M4
	|\ \
	| | * 7_G
	| | * 7_F
	| * ~ 7_E
	| * ~ 7_D
	* | ~ 7_C
	| |/
	|/|
	* | 7_B
	|/
	* 7_A
	EOF
'

test_expect_success 'log --graph --graph-lane-limit=1 truncate mid octopus merge' '
	check_graph --graph-lane-limit=1 M_7 <<-\EOF
	*-~  7_M4
	|\~
	| ~ 7_G
	| ~ 7_F
	| * 7_E
	| * 7_D
	* ~ 7_C
	| ~
	|/~
	* ~ 7_B
	|/
	* 7_A
	EOF
'

test_expect_success 'log --graph --graph-lane-limit=3 limited to three lanes' '
	check_graph --graph-lane-limit=3 M_1 M_3 M_5 M_7 <<-\EOF
	*   7_M1
	|\
	| | *   7_M2
	| | |\
	| | | * 7_H
	| | | ~ 7_M3
	| | | ~ 7_J
	| | | ~ 7_I
	| | | ~ 7_M4
	| |_|_~
	|/| | ~
	| | |_~
	| |/| ~
	| | | ~
	| | |/~
	| | * ~ 7_G
	| | | ~
	| | |/~
	| | * ~ 7_F
	| * | ~ 7_E
	| | |/~
	| |/| ~
	| * | ~ 7_D
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

test_expect_success 'log --graph --graph-lane-limit=6 check if it only shows first of 3 parent merge' '
	check_graph --graph-lane-limit=6 M_1 M_3 M_5 M_7 <<-\EOF
	*   7_M1
	|\
	| | *   7_M2
	| | |\
	| | | * 7_H
	| | | | *   7_M3
	| | | | |\
	| | | | | * 7_J
	| | | | * | 7_I
	| | | | | | * 7_M4
	| |_|_|_|_|/~
	|/| | | | |/~
	| | |_|_|/| ~
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

test_expect_success 'log --graph --graph-lane-limit=7 check if it shows all 3 parent merge' '
	check_graph --graph-lane-limit=7 M_1 M_3 M_5 M_7 <<-\EOF
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

test_expect_success 'log --graph with root commit' '
	git checkout --orphan 8_1 && test_commit 8_A && test_commit 8_A1 &&
	git checkout --orphan 8_2 && test_commit 8_B &&

	check_graph 8_2 8_1 <<-\EOF
	* 8_B
	  * 8_A1
	 /
	* 8_A
	EOF
'

test_expect_success 'log --graph with multiple root commits' '
	test_commit 8_B1 &&
	git checkout --orphan 8_3 && test_commit 8_C &&

	check_graph 8_3 8_2 8_1 <<-\EOF
	* 8_C
	  * 8_B1
	 /
	* 8_B
	  * 8_A1
	 /
	* 8_A
	EOF
'

test_expect_success 'log --graph commit from a two parent merge shifted' '
	git checkout --orphan 9_1 && test_commit 9_B &&
	git checkout --orphan 9_2 && test_commit 9_C &&
	git checkout 9_1 &&
	git merge 9_2 --allow-unrelated-histories -m 9_M &&
	git checkout --orphan 9_3 &&
	test_commit 9_A && test_commit 9_A1 && test_commit 9_A2 &&

	check_graph 9_3 9_1 <<-\EOF
	* 9_A2
	* 9_A1
	* 9_A
	  * 9_M
	 /|
	| * 9_C
	* 9_B
	EOF
'

test_expect_success 'log --graph commit from a three parent merge shifted' '
	git checkout --orphan 10_1 && test_commit 10_B &&
	git checkout --orphan 10_2 && test_commit 10_C &&
	git checkout --orphan 10_3 && test_commit 10_D &&
	git checkout 10_1 &&
	TREE=$(git write-tree) &&
	MERGE=$(git commit-tree $TREE -p 10_1 -p 10_2 -p 10_3 -m 10_M) &&
	git reset --hard $MERGE &&
	git checkout --orphan 10_4 &&
	test_commit 10_A && test_commit 10_A1 && test_commit 10_A2 &&

	check_graph 10_4 10_1 <<-\EOF
	* 10_A2
	* 10_A1
	* 10_A
	  *   10_M
	 /|\
	| | * 10_D
	| * 10_C
	* 10_B
	EOF
'

test_expect_success 'log --graph commit from a four parent merge shifted' '
	git checkout --orphan 11_1 && test_commit 11_B &&
	git checkout --orphan 11_2 && test_commit 11_C &&
	git checkout --orphan 11_3 && test_commit 11_D &&
	git checkout --orphan 11_4 && test_commit 11_E &&
	git checkout 11_1 &&
	TREE=$(git write-tree) &&
	MERGE=$(git commit-tree $TREE -p 11_1 -p 11_2 -p 11_3 -p 11_4 -m 11_M) &&
	git reset --hard $MERGE &&
	git checkout --orphan 11_5 &&
	test_commit 11_A && test_commit 11_A1 && test_commit 11_A2 &&

	check_graph 11_5 11_1 <<-\EOF
	* 11_A2
	* 11_A1
	* 11_A
	  *-.   11_M
	 /|\ \
	| | | * 11_E
	| | * 11_D
	| * 11_C
	* 11_B
	EOF
'

test_expect_success 'log --graph disconnected three roots cascading' '
	git checkout --orphan 12_1 && test_commit 12_D && test_commit 12_D1 &&
	git checkout --orphan 12_2 && test_commit 12_C &&
	git checkout --orphan 12_3 && test_commit 12_B &&
	git checkout --orphan 12_4 && test_commit 12_A &&

	check_graph 12_4 12_3 12_2 12_1 <<-\EOF
	* 12_A
	  * 12_B
	    * 12_C
	      * 12_D1
	   _ /
	  /
	 /
	* 12_D
	EOF
'

test_expect_success 'log --graph with excluded parent (not a root)' '
	git checkout --orphan 13_1 && test_commit 13_X && test_commit 13_Y &&
	git checkout --orphan 13_2 && test_commit 13_O && test_commit 13_A &&

	check_graph 13_O..13_A 13_1 <<-\EOF
	* 13_A
	  * 13_Y
	 /
	* 13_X
	EOF
'

test_done
