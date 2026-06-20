#!/bin/sh

test_description='git log --graph visual root indentations'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-log-graph.sh

check_graph_with_description () {
	cat >expect &&
	lib_test_cmp_graph --format="%s%ndescription%nsecond-line" "$@"
}

create_orphan () {
	git checkout --orphan "$1" &&
	{ git rm -rf . || true; }
}

# disable commit-graph topo order to have the graph to render in different
# ways (used in --first-parent tests to have multiple visual roots while a
# column is active at the same time).
unset_commit_graph() {
	sane_unset GIT_TEST_COMMIT_GRAPH &&
	rm -f .git/objects/info/commit-graph &&
	rm -rf .git/objects/info/commit-graphs
}

test_expect_success 'single root commit is not indented' '
	create_orphan _1 && test_commit 1_A &&
	lib_test_check_graph _1 <<-\EOF
	* 1_A
	EOF
'

test_expect_success 'visual root indented before unrelated branch' '
	create_orphan _2 && test_commit 2_A && test_commit 2_B &&
	create_orphan _3 && test_commit 3_A &&
	lib_test_check_graph _2 _3 <<-\EOF
	  * 3_A
	* 2_B
	* 2_A
	EOF
'

test_expect_success 'visual root indentation with --left-right' '
	lib_test_check_graph --left-right _2..._3 <<-\EOF
	  > 3_A
	< 2_B
	< 2_A
	EOF
'

# A better case of why indentation is still needed with '--left-right' flag is
# that unrelated branches can be on the same side, so it's needed to
# differentiate visual roots on the same side.
test_expect_success 'visual root indentation with --left-right having unrelated commits on the same side' '
	lib_test_check_graph --left-right _2..._3 _1 <<-\EOF
	  > 3_A
	< 2_B
	 \
	  < 2_A
	> 1_A
	EOF
'

test_expect_success 'visual root indents the description also' '
	check_graph_with_description _2 _3 <<-\EOF
	  * 3_A
	    description
	    second-line
	* 2_B
	| description
	| second-line
	* 2_A
	  description
	  second-line
	EOF
'

test_expect_success 'indented visual root parent gets connected to its child' '
	create_orphan _4 && test_commit 4_A && test_commit 4_B &&
	create_orphan _5 && test_commit 5_A && test_commit 5_B &&
	lib_test_check_graph _4 _5<<-\EOF
	* 5_B
	 \
	  * 5_A
	* 4_B
	* 4_A
	EOF
'

test_expect_success 'indented visual root parent gets connected to its child with description' '
	check_graph_with_description _4 _5 <<-\EOF
	* 5_B
	| description
	| second-line
	 \
	  * 5_A
	    description
	    second-line
	* 4_B
	| description
	| second-line
	* 4_A
	  description
	  second-line
	EOF
'

test_expect_success 'visual roots cascade and last root does not' '
	create_orphan _7 && test_commit 7_A && test_commit 7_B &&
	create_orphan _8 && test_commit 8_A &&
	create_orphan _9 && test_commit 9_A &&
	create_orphan _10 && test_commit 10_A &&
	lib_test_check_graph _7 _8 _9 _10  <<-\EOF
	* 10_A
	  * 9_A
	    * 8_A
	* 7_B
	* 7_A
	EOF
'

test_expect_success 'last root does not cascade' '
	lib_test_check_graph _8 _9 _10 <<-\EOF
	* 10_A
	  * 9_A
	* 8_A
	EOF
'

test_expect_success 'merge parents are roots between them but they do not indent' '
	create_orphan _11 && test_commit 11_A &&
	create_orphan _12 && test_commit 12_A &&
	create_orphan _13 && test_commit 13_A &&
	git checkout _11 &&
	TREE=$(git write-tree) &&
	MERGE=$(git commit-tree $TREE -p _11 -p _12 -p _13 -m 11_octopus) &&
	git reset --hard $MERGE &&
	lib_test_check_graph _11 <<-\EOF
	*-.   11_octopus
	|\ \
	| | * 13_A
	| * 12_A
	* 11_A
	EOF
'

# The last parent of a merge can be indented if nothing related to it needs to
# be rendered after, if it's another visual root, merge parent must not get
# indented but rather activate cascading.
test_expect_success 'merge then unrelated visual root and unrelated branch' '
	create_orphan _16 && test_commit 16_A && test_commit 16_B &&
	create_orphan _17 && test_commit 17_A &&
	create_orphan _18 && test_commit 18_A &&
	create_orphan _19 && test_commit 19_A &&
	create_orphan _20 && test_commit 20_A &&
	git checkout _18 &&
	TREE=$(git write-tree) &&
	MERGE=$(git commit-tree $TREE -p _18 -p _19 -p _20 -m 18_octopus) &&
	git reset --hard $MERGE &&
	lib_test_check_graph _18 _17 _16 <<-\EOF
	*-.   18_octopus
	|\ \
	| | * 20_A
	| * 19_A
	* 18_A
	  * 17_A
	* 16_B
	* 16_A
	EOF
'

# The last commit root does not get indented, if the next thing after the root
# merge parent is the last commit, indent the merge parent.
test_expect_success 'merge then unrelated root indents merge parent' '
	lib_test_check_graph _18 _17 <<-\EOF
	*-.   18_octopus
	|\ \
	| | * 20_A
	| * 19_A
	 \
	  * 18_A
	* 17_A
	EOF
'

test_expect_success 'merge then unrelated branch indents merge parent' '
	lib_test_check_graph _18 _16 <<-\EOF
	*-.   18_octopus
	|\ \
	| | * 20_A
	| * 19_A
	 \
	  * 18_A
	* 16_B
	* 16_A
	EOF
'

test_expect_success 'two-parent merge of orphans' '
	create_orphan _21 && test_commit 21_A &&
	create_orphan _22 && test_commit 22_A &&
	git checkout _21 &&
	TREE=$(git write-tree) &&
	MERGE=$(git commit-tree $TREE -p _21 -p _22 -m 21_merge) &&
	git reset --hard $MERGE &&
	lib_test_check_graph _21 <<-\EOF
	*   21_merge
	|\
	| * 22_A
	* 21_A
	EOF
'

test_expect_success 'commit with filtered parent becomes a visual root' '
	create_orphan _23 &&
	echo test >other.txt &&
	git add other.txt &&
	git commit -m "23_A" &&
	echo test >foo.txt &&
	git add foo.txt &&
	git commit -m "23_B" &&
	create_orphan _24 &&
	echo test >foo.txt &&
	git add foo.txt &&
	git commit -m "24_A" &&
	lib_test_check_graph _23 _24 -- foo.txt <<-\EOF
	  * 23_B
	* 24_A
	EOF
'

# The walker simplifies the commit for the current one and its parents, removing
# the filtered parents, but it doesn't go one step ahead, this causes some edge
# cases with the lookahead.
# Given A (orphan), the walker only processes A, and when we lookahead for B
# (child of C) even tho C will be filtered, it hasn't been simplified yet, so we
# don't see B as a visual root, therefore cascade indentation isn't applied to A.
# (cascade indentation starts the indentation at the second visual root, to avoid
# redundant indentation). So A gets an extra indent, and once B is processed,
# when rendering it, C has been removed, B is a visual root and as the last commit
# isn't considered a visual root as it cannot have unrelated commits below it,
# cascading isn't also applied, giving B another indent.
#
# The final result is an extra indent for A and B:
#
#	  A
#	    B
#	D
#
# This will happen for any case where we find ourselves with the next commit
# being a unrelated child of a parent the will be filtered.
#
# instead of the expected:
test_expect_failure 'filtered parent cascading edge case' '
	create_orphan _25 &&
	git rm -rf . &&
	echo test >other.txt &&
	git add other.txt &&
	git commit -m "C-filtered" &&

	echo test >foo.txt &&
	git add foo.txt &&
	git commit -m "B (child of filtered)" &&

	create_orphan _26 &&
	git rm -rf . &&
	echo test >foo.txt &&
	git add foo.txt &&
	git commit -m "A (visual root)" &&

	create_orphan _27 &&
	git rm -rf . &&
	echo test >foo.txt &&
	git add foo.txt &&
	git commit -m "D (last)" &&

	lib_test_check_graph _25 _26 _27 -- foo.txt <<-\EOF
	* A (visual root)
	  * B (child of filtered)
	* D (last)
	EOF
'

test_expect_failure 'multiple filtered parents in sequence' '
	create_orphan _44 &&
	git rm -rf . &&
	echo a >other.txt && git add other.txt && git commit -m "44_F" &&
	echo b >foo.txt && git add foo.txt && git commit -m "44_C" &&

	create_orphan _45 &&
	git rm -rf . &&
	echo c >other.txt && git add other.txt && git commit -m "45_F" &&
	echo d >foo.txt && git add foo.txt && git commit -m "45_C" &&

	create_orphan _46 &&
	git rm -rf . &&
	echo e >foo.txt && git add foo.txt && git commit -m "46_A" &&

	lib_test_check_graph _44 _45 _46 -- foo.txt <<-\EOF
	* 46_A
	  * 45_C
	* 44_C
	EOF
'

test_expect_failure 'real orphan root followed by child of filtered parent' '
	create_orphan _47 &&
	git rm -rf . &&
	echo a >foo.txt && git add foo.txt && git commit -m "47_A" &&

	create_orphan _48 &&
	git rm -rf . &&
	echo b >other.txt && git add other.txt && git commit -m "48_filtered" &&
	echo c >foo.txt && git add foo.txt && git commit -m "48_B" &&

	create_orphan _49 &&
	git rm -rf . &&
	echo d >foo.txt && git add foo.txt && git commit -m "49_last" &&

	lib_test_check_graph _47 _48 _49 -- foo.txt <<-\EOF
	* 47_A
	  * 48_B
	* 49_last
	EOF
'

# This tests prove why there is no need to have indentation for boundary
# commits.
#
# Boundary commits rather than starting a column they 'inherit' the one of
# its child so there will always be an edge that connects it removing the
# ambiguity.
test_expect_success 'unrelated boundaries are not ambiguous' '
	create_orphan _28 && test_commit 28_A && test_commit 28_B &&
	test_commit 28_C &&
	create_orphan _29 && test_commit 29_A && test_commit 29_B &&
	lib_test_check_graph --boundary 28_A.._28 29_A.._29 <<-\EOF
	* 29_B
	| * 28_C
	| * 28_B
	| o 28_A
	o 29_A
	EOF
'

# Same structure as t6016
test_expect_success 'boundary commits big test' '
	# 3 commits on branch _30
	create_orphan _30 &&
	test_commit 30_A &&
	test_commit 30_B &&
	test_commit 30_C &&

	# 2 commits on branch _31, started from 30_A
	git checkout -b _31 30_A &&
	test_commit 31_A &&
	test_commit 31_B &&

	# 2 commits on branch _32, started from 30_B
	git checkout -b _32 30_B &&
	test_commit 32_A &&
	test_commit 32_B &&

	# Octopus merge _31 and _32 into -30
	git checkout _30 &&
	git merge _31 _32 -m 30_D &&
	git tag 30_D &&
	test_commit 30_E &&

	# More commits on _32, then merge _32 into _30
	git checkout _32 &&
	test_commit 32_C &&
	test_commit 32_D &&
	git checkout _30 &&
	git merge -s ours _32 -m 30_F &&
	git tag 30_F &&
	test_commit 30_G &&
	lib_test_check_graph --boundary _30 _31 _32 ^32_C <<-\EOF
	* 30_G
	*   30_F
	|\
	| * 32_D
	* | 30_E
	| |
	|  \
	*-. \   30_D
	|\ \ \
	| * | | 31_B
	| * | | 31_A
	* | | | 30_C
	o | | | 30_B
	|/ / /
	o / / 30_A
	 / /
	| o 32_C
	|/
	o 32_B
	EOF
'

# Filter by --first-parent and then forcing the filtered parents to be shown.
test_expect_success '--first-parent flag with the filtered parents' '
	(
		unset_commit_graph &&
		create_orphan _35 && test_commit 35_A && test_commit 35_B &&
		create_orphan _36 && test_commit 36_A &&
		create_orphan _37 && test_commit 37_A &&
		git checkout _35 &&
		TREE=$(git write-tree) &&
		MERGE=$(git commit-tree $TREE -p _35 -p _36 -p _37 -m 35_octopus) &&
		git reset --hard $MERGE &&
		lib_test_check_graph --first-parent _35 _36 _37 <<-\EOF
		* 35_octopus
		| * 37_A
		|   * 36_A
		* 35_B
		* 35_A
		EOF
	)
'

test_expect_success '--first-parent with filtered parents but one has a child' '
	(
		unset_commit_graph &&
		create_orphan _38 && test_commit 38_A && test_commit 38_B &&
		create_orphan _39 && test_commit 39_A &&
		create_orphan _40 && test_commit 40_A && test_commit 40_B &&
		git checkout _38 &&
		TREE=$(git write-tree) &&
		MERGE=$(git commit-tree $TREE -p _38 -p _39 -p _40 -m 38_octopus) &&
		git reset --hard $MERGE &&
		lib_test_check_graph --first-parent _38 _39 _40 <<-\EOF
		* 38_octopus
		| * 40_B
		| * 40_A
		|   * 39_A
		* 38_B
		* 38_A
		EOF
	)
'

test_expect_success '--first-parent with filtered parents but both have childs' '
	(
		unset_commit_graph &&
		create_orphan _41 && test_commit 41_A && test_commit 41_B &&
		create_orphan _42 && test_commit 42_A && test_commit 42_B &&
		create_orphan _43 && test_commit 43_A && test_commit 43_B &&
		git checkout _41 &&
		TREE=$(git write-tree) &&
		MERGE=$(git commit-tree $TREE -p _41 -p _42 -p _43 -m 41_octopus) &&
		git reset --hard $MERGE &&
		lib_test_check_graph --first-parent _41 _42 _43 <<-\EOF
		* 41_octopus
		| * 43_B
		|  \
		|   * 43_A
		| * 42_B
		| * 42_A
		* 41_B
		* 41_A
		EOF
	)
'

test_done
