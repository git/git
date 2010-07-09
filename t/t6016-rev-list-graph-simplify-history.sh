#!/bin/sh

# There's more than one "correct" way to represent the history graphically.
# These tests depend on the current behavior of the graphing code.  If the
# graphing code is ever changed to draw the output differently, these tests
# cases will need to be updated to know about the new layout.

test_description='--graph and simplified history'

. ./test-lib.sh

test_expect_success 'set up rev-list --graph test' '
	# 3 commits on branch A
	test_commit A1 foo.txt &&
	test_commit A2 bar.txt &&
	test_commit A3 bar.txt &&
	git branch -m master A &&

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
	git merge B C &&
	git tag A4

	test_commit A5 bar.txt &&

	# More commits on C, then merge C into A
	git checkout C &&
	test_commit C3 foo.txt &&
	test_commit C4 bar.txt &&
	git checkout A &&
	git merge -s ours C &&
	git tag A6

	test_commit A7 bar.txt &&

	# Store commit names in variables for later use
	A1=$(git rev-parse --verify A1) &&
	A2=$(git rev-parse --verify A2) &&
	A3=$(git rev-parse --verify A3) &&
	A4=$(git rev-parse --verify A4) &&
	A5=$(git rev-parse --verify A5) &&
	A6=$(git rev-parse --verify A6) &&
	A7=$(git rev-parse --verify A7) &&
	B1=$(git rev-parse --verify B1) &&
	B2=$(git rev-parse --verify B2) &&
	C1=$(git rev-parse --verify C1) &&
	C2=$(git rev-parse --verify C2) &&
	C3=$(git rev-parse --verify C3) &&
	C4=$(git rev-parse --verify C4)
	'

test_expect_success '--graph --all' '
	rm -f expected &&
	echo "* $A7" >> expected &&
	echo "*   $A6" >> expected &&
	echo "|\\  " >> expected &&
	echo "| * $C4" >> expected &&
	echo "| * $C3" >> expected &&
	echo "* | $A5" >> expected &&
	echo "| |     " >> expected &&
	echo "|  \\    " >> expected &&
	echo "*-. \\   $A4" >> expected &&
	echo "|\\ \\ \\  " >> expected &&
	echo "| | |/  " >> expected &&
	echo "| | * $C2" >> expected &&
	echo "| | * $C1" >> expected &&
	echo "| * | $B2" >> expected &&
	echo "| * | $B1" >> expected &&
	echo "* | | $A3" >> expected &&
	echo "| |/  " >> expected &&
	echo "|/|   " >> expected &&
	echo "* | $A2" >> expected &&
	echo "|/  " >> expected &&
	echo "* $A1" >> expected &&
	git rev-list --graph --all > actual &&
	test_cmp expected actual
	'

# Make sure the graph_is_interesting() code still realizes
# that undecorated merges are interesting, even with --simplify-by-decoration
test_expect_success '--graph --simplify-by-decoration' '
	rm -f expected &&
	git tag -d A4
	echo "* $A7" >> expected &&
	echo "*   $A6" >> expected &&
	echo "|\\  " >> expected &&
	echo "| * $C4" >> expected &&
	echo "| * $C3" >> expected &&
	echo "* | $A5" >> expected &&
	echo "| |     " >> expected &&
	echo "|  \\    " >> expected &&
	echo "*-. \\   $A4" >> expected &&
	echo "|\\ \\ \\  " >> expected &&
	echo "| | |/  " >> expected &&
	echo "| | * $C2" >> expected &&
	echo "| | * $C1" >> expected &&
	echo "| * | $B2" >> expected &&
	echo "| * | $B1" >> expected &&
	echo "* | | $A3" >> expected &&
	echo "| |/  " >> expected &&
	echo "|/|   " >> expected &&
	echo "* | $A2" >> expected &&
	echo "|/  " >> expected &&
	echo "* $A1" >> expected &&
	git rev-list --graph --all --simplify-by-decoration > actual &&
	test_cmp expected actual
	'

# Get rid of all decorations on branch B, and graph with it simplified away
test_expect_success '--graph --simplify-by-decoration prune branch B' '
	rm -f expected &&
	git tag -d B2
	git tag -d B1
	git branch -d B
	echo "* $A7" >> expected &&
	echo "*   $A6" >> expected &&
	echo "|\\  " >> expected &&
	echo "| * $C4" >> expected &&
	echo "| * $C3" >> expected &&
	echo "* | $A5" >> expected &&
	echo "* |   $A4" >> expected &&
	echo "|\\ \\  " >> expected &&
	echo "| |/  " >> expected &&
	echo "| * $C2" >> expected &&
	echo "| * $C1" >> expected &&
	echo "* | $A3" >> expected &&
	echo "|/  " >> expected &&
	echo "* $A2" >> expected &&
	echo "* $A1" >> expected &&
	git rev-list --graph --simplify-by-decoration --all > actual &&
	test_cmp expected actual
	'

test_expect_success '--graph --full-history -- bar.txt' '
	rm -f expected &&
	git tag -d B2
	git tag -d B1
	git branch -d B
	echo "* $A7" >> expected &&
	echo "*   $A6" >> expected &&
	echo "|\\  " >> expected &&
	echo "| * $C4" >> expected &&
	echo "* | $A5" >> expected &&
	echo "* |   $A4" >> expected &&
	echo "|\\ \\  " >> expected &&
	echo "| |/  " >> expected &&
	echo "* | $A3" >> expected &&
	echo "|/  " >> expected &&
	echo "* $A2" >> expected &&
	git rev-list --graph --full-history --all -- bar.txt > actual &&
	test_cmp expected actual
	'

test_expect_success '--graph --full-history --simplify-merges -- bar.txt' '
	rm -f expected &&
	git tag -d B2
	git tag -d B1
	git branch -d B
	echo "* $A7" >> expected &&
	echo "*   $A6" >> expected &&
	echo "|\\  " >> expected &&
	echo "| * $C4" >> expected &&
	echo "* | $A5" >> expected &&
	echo "* | $A3" >> expected &&
	echo "|/  " >> expected &&
	echo "* $A2" >> expected &&
	git rev-list --graph --full-history --simplify-merges --all \
		-- bar.txt > actual &&
	test_cmp expected actual
	'

test_expect_success '--graph -- bar.txt' '
	rm -f expected &&
	git tag -d B2
	git tag -d B1
	git branch -d B
	echo "* $A7" >> expected &&
	echo "* $A5" >> expected &&
	echo "* $A3" >> expected &&
	echo "| * $C4" >> expected &&
	echo "|/  " >> expected &&
	echo "* $A2" >> expected &&
	git rev-list --graph --all -- bar.txt > actual &&
	test_cmp expected actual
	'

test_expect_success '--graph --sparse -- bar.txt' '
	rm -f expected &&
	git tag -d B2
	git tag -d B1
	git branch -d B
	echo "* $A7" >> expected &&
	echo "* $A6" >> expected &&
	echo "* $A5" >> expected &&
	echo "* $A4" >> expected &&
	echo "* $A3" >> expected &&
	echo "| * $C4" >> expected &&
	echo "| * $C3" >> expected &&
	echo "| * $C2" >> expected &&
	echo "| * $C1" >> expected &&
	echo "|/  " >> expected &&
	echo "* $A2" >> expected &&
	echo "* $A1" >> expected &&
	git rev-list --graph --sparse --all -- bar.txt > actual &&
	test_cmp expected actual
	'

test_expect_success '--graph ^C4' '
	rm -f expected &&
	echo "* $A7" >> expected &&
	echo "* $A6" >> expected &&
	echo "* $A5" >> expected &&
	echo "*   $A4" >> expected &&
	echo "|\\  " >> expected &&
	echo "| * $B2" >> expected &&
	echo "| * $B1" >> expected &&
	echo "* $A3" >> expected &&
	git rev-list --graph --all ^C4 > actual &&
	test_cmp expected actual
	'

test_expect_success '--graph ^C3' '
	rm -f expected &&
	echo "* $A7" >> expected &&
	echo "*   $A6" >> expected &&
	echo "|\\  " >> expected &&
	echo "| * $C4" >> expected &&
	echo "* $A5" >> expected &&
	echo "*   $A4" >> expected &&
	echo "|\\  " >> expected &&
	echo "| * $B2" >> expected &&
	echo "| * $B1" >> expected &&
	echo "* $A3" >> expected &&
	git rev-list --graph --all ^C3 > actual &&
	test_cmp expected actual
	'

# I don't think the ordering of the boundary commits is really
# that important, but this test depends on it.  If the ordering ever changes
# in the code, we'll need to update this test.
test_expect_success '--graph --boundary ^C3' '
	rm -f expected &&
	echo "* $A7" >> expected &&
	echo "*   $A6" >> expected &&
	echo "|\\  " >> expected &&
	echo "| * $C4" >> expected &&
	echo "* | $A5" >> expected &&
	echo "| |     " >> expected &&
	echo "|  \\    " >> expected &&
	echo "*-. \\   $A4" >> expected &&
	echo "|\\ \\ \\  " >> expected &&
	echo "| * | | $B2" >> expected &&
	echo "| * | | $B1" >> expected &&
	echo "* | | | $A3" >> expected &&
	echo "o | | | $A2" >> expected &&
	echo "|/ / /  " >> expected &&
	echo "o | | $A1" >> expected &&
	echo " / /  " >> expected &&
	echo "| o $C3" >> expected &&
	echo "|/  " >> expected &&
	echo "o $C2" >> expected &&
	git rev-list --graph --boundary --all ^C3 > actual &&
	test_cmp expected actual
	'

test_done
