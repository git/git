#!/bin/sh

# There's more than one "correct" way to represent the history graphically.
# These tests depend on the current behavior of the graphing code.  If the
# graphing code is ever changed to draw the output differently, these tests
# cases will need to be updated to know about the new layout.

test_description='--graph and simplified history'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-log-graph.sh

check_graph () {
	cat >expect &&
	lib_test_cmp_graph --format=%s "$@"
}

test_expect_success 'set up rev-list --graph test' '
	# 3 commits on branch A
	test_commit A1 foo.txt &&
	test_commit A2 bar.txt &&
	test_commit A3 bar.txt &&
	git branch -m main A &&

	# 2 commits on branch B, started from A1
	git checkout -b B A1 &&
	test_commit B1 foo.txt &&
	test_commit B2 abc.txt &&

	# 2 commits on branch C, started from A2
	git checkout -b C A2 &&
	test_commit C1 xyz.txt &&
	test_commit C2 xyz.txt &&

	# Octopus merge B and C into branch A
	git checkout A &&
	git merge B C -m A4 &&
	git tag A4 &&

	test_commit A5 bar.txt &&

	# More commits on C, then merge C into A
	git checkout C &&
	test_commit C3 foo.txt &&
	test_commit C4 bar.txt &&
	git checkout A &&
	git merge -s ours C -m A6 &&
	git tag A6 &&

	test_commit A7 bar.txt
'

test_expect_success '--graph --all' '
	check_graph --all <<-\EOF
	* A7
	*   A6
	|\
	| * C4
	| * C3
	* | A5
	| |
	|  \
	*-. | A4
	|\ \|
	| | * C2
	| | * C1
	| * | B2
	| * | B1
	* | | A3
	| |/
	|/|
	* | A2
	|/
	* A1
	EOF
'

# Make sure the graph_is_interesting() code still realizes
# that undecorated merges are interesting, even with --simplify-by-decoration
test_expect_success '--graph --simplify-by-decoration' '
	git tag -d A4 &&
	check_graph --all --simplify-by-decoration <<-\EOF
	* A7
	*   A6
	|\
	| * C4
	| * C3
	* | A5
	| |
	|  \
	*-. | A4
	|\ \|
	| | * C2
	| | * C1
	| * | B2
	| * | B1
	* | | A3
	| |/
	|/|
	* | A2
	|/
	* A1
	EOF
'

test_expect_success 'setup: get rid of decorations on B' '
	git tag -d B2 &&
	git tag -d B1 &&
	git branch -d B
'

# Graph with branch B simplified away
test_expect_success '--graph --simplify-by-decoration prune branch B' '
	check_graph --simplify-by-decoration --all <<-\EOF
	* A7
	*   A6
	|\
	| * C4
	| * C3
	* | A5
	* | A4
	|\|
	| * C2
	| * C1
	* | A3
	|/
	* A2
	* A1
	EOF
'

test_expect_success '--graph --full-history -- bar.txt' '
	check_graph --full-history --all -- bar.txt <<-\EOF
	* A7
	*   A6
	|\
	| * C4
	* | A5
	* | A4
	|\|
	* | A3
	|/
	* A2
	EOF
'

test_expect_success '--graph --full-history --simplify-merges -- bar.txt' '
	check_graph --full-history --simplify-merges --all -- bar.txt <<-\EOF
	* A7
	*   A6
	|\
	| * C4
	* | A5
	* | A3
	|/
	* A2
	EOF
'

test_expect_success '--graph -- bar.txt' '
	check_graph --all -- bar.txt <<-\EOF
	* A7
	* A5
	* A3
	| * C4
	|/
	* A2
	EOF
'

test_expect_success '--graph --sparse -- bar.txt' '
	check_graph --sparse --all -- bar.txt <<-\EOF
	* A7
	* A6
	* A5
	* A4
	* A3
	| * C4
	| * C3
	| * C2
	| * C1
	|/
	* A2
	* A1
	EOF
'

test_expect_success '--graph ^C4' '
	check_graph --all ^C4 <<-\EOF
	* A7
	* A6
	* A5
	*   A4
	|\
	| * B2
	| * B1
	* A3
	EOF
'

test_expect_success '--graph ^C3' '
	check_graph --all ^C3 <<-\EOF
	* A7
	*   A6
	|\
	| * C4
	* A5
	*   A4
	|\
	| * B2
	| * B1
	* A3
	EOF
'

# I don't think the ordering of the boundary commits is really
# that important, but this test depends on it.  If the ordering ever changes
# in the code, we'll need to update this test.
test_expect_success '--graph --boundary ^C3' '
	check_graph --boundary --all ^C3 <<-\EOF
	* A7
	*   A6
	|\
	| * C4
	* | A5
	| |
	|  \
	*-. \   A4
	|\ \ \
	| * | | B2
	| * | | B1
	* | | | A3
	o | | | A2
	|/ / /
	o / / A1
	 / /
	| o C3
	|/
	o C2
	EOF
'

test_done
