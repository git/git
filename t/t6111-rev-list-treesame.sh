#!/bin/sh
#
#        ,---E--.   *H----------.             * marks !TREESAME parent paths
#       /        \ /             \*
# *A--*B---D--*F-*G---------K-*L-*M
#   \     /*       \       /
#    `-C-'          `-*I-*J
#
# A creates "file", B and F change it.
# Odd merge G takes the old version from B.
# I changes it, but J reverts it, so K is TREESAME to both parents.
# H and L both change "file", and M merges those changes.

test_description='TREESAME and limiting'

. ./test-lib.sh

note () {
	git tag "$1"
}

unnote () {
	git name-rev --tags --stdin | sed -e "s|$_x40 (tags/\([^)]*\))\([ 	]\)|\1\2|g"
}

test_expect_success setup '
	test_commit "Initial file" file "Hi there" A &&
	git branch other-branch &&

	test_commit "file=Hello" file "Hello" B &&
	git branch third-branch &&

	git checkout other-branch &&
	test_commit "Added other" other "Hello" C &&

	git checkout master &&
	test_merge D other-branch &&

	git checkout third-branch &&
	test_commit "Third file" third "Nothing" E &&

	git checkout master &&
	test_commit "file=Blah" file "Blah" F &&

	test_tick && git merge --no-commit third-branch &&
	git checkout third-branch file &&
	git commit &&
	note G &&
	git branch fiddler-branch &&

	git checkout -b part2-branch &&
	test_commit "file=Part 2" file "Part 2" H &&

	git checkout fiddler-branch &&
	test_commit "Bad commit" file "Silly" I &&

	test_tick && git revert I && note J &&

	git checkout master &&
	test_tick && git merge --no-ff fiddler-branch &&
	note K

	test_commit "file=Part 1" file "Part 1" L &&

	test_tick && test_must_fail git merge part2-branch &&
	test_commit M file "Parts 1+2"
'

check_outcome () {
	outcome=$1
	shift

	case "$1" in
	*"("*)
		FMT="%P	%H | %s"
		munge_actual="
			s/^\([^	]*\)	\([^ ]*\) .*/(\1)\2/
			s/ //g
			s/()//
		"
		;;
	*)
		FMT="%H | %s"
		munge_actual="s/^\([^ ]*\) .*/\1/"
		;;
	esac &&
	printf "%s\n" $1 >expect &&
	shift

	param="$*" &&
	test_expect_$outcome "log $param" '
		git log --format="$FMT" $param |
		unnote >actual &&
		sed -e "$munge_actual" <actual >check &&
		test_cmp expect check || {
			cat actual
			false
		}
	'
}

check_result () {
	check_outcome success "$@"
}

# Odd merge G drops a change in F. Important that G is listed in all
# except the most basic list. Achieving this means normal merge D will also be
# shown in normal full-history, as we can't distinguish unless we do a
# simplification pass. After simplification, D is dropped but G remains.
# Also, merge simplification of G should not drop the parent B that the default
# simple history follows.
check_result 'M L K J I H G F E D C B A'
check_result '(LH)M (K)L (GJ)K (I)J (G)I (G)H (FE)G (D)F (B)E (BC)D (A)C (A)B A'
check_result 'M H L K J I G E F D C B A' --topo-order
check_result 'M L H B A' -- file
check_result '(LH)M (B)L (B)H (A)B A' --parents -- file
check_result 'M L J I H G F D B A' --full-history -- file
check_result '(LH)M (K)L (GJ)K (I)J (G)I (G)H (FB)G (D)F (BA)D (A)B A' --full-history --parents -- file
check_result '(LH)M (G)H (J)L (I)J (G)I (FB)G (B)F (A)B A' --simplify-merges -- file
check_result 'M L K G F D B A' --first-parent
check_result 'M L G F B A' --first-parent -- file

# Check that odd merge G remains shown when F is the bottom.
check_result 'M L K J I H G E' F..M
check_result 'M H L K J I G E' F..M --topo-order
check_result 'M L H' F..M -- file
check_result '(LH)M (B)L (B)H' --parents F..M -- file
check_result 'M L J I H G' F..M --full-history -- file
check_result '(LH)M (K)L (GJ)K (I)J (G)I (G)H (FB)G' F..M --full-history --parents -- file
check_result '(LH)M (G)H (J)L (I)J (G)I (FB)G' F..M --simplify-merges -- file
check_result 'M L K J I H G' F..M --ancestry-path
check_result 'M L J I H G' F..M --ancestry-path -- file
check_result '(LH)M (K)L (GJ)K (I)J (G)I (G)H (FE)G' F..M --ancestry-path --parents -- file
check_result '(LH)M (G)H (J)L (I)J (G)I (FE)G' F..M --ancestry-path --simplify-merges -- file
check_result 'M L K G' F..M --first-parent
check_result 'M L G' F..M --first-parent -- file

# Note that G is pruned when E is the bottom, even if it's the same commit list
# If we want history since E, then we're quite happy to ignore G that took E.
check_result 'M L K J I H G' E..M --ancestry-path
check_result 'M L J I H' E..M --ancestry-path -- file
check_result '(LH)M (K)L (EJ)K (I)J (E)I (E)H' E..M --ancestry-path --parents -- file
check_result '(LH)M (E)H (J)L (I)J (E)I' E..M --ancestry-path --simplify-merges -- file

# Should still be able to ignore I-J branch in simple log, despite limiting
# to G.
check_result 'M L K J I H' G..M
check_result 'M H L K J I' G..M --topo-order
check_result 'M L H' G..M -- file
check_result '(LH)M (G)L (G)H' G..M --parents -- file
check_result 'M L J I H' G..M --full-history -- file
check_result 'M L K J I H' G..M --full-history --parents -- file
check_result 'M H L J I' G..M --simplify-merges -- file
check_result 'M L K J I H' G..M --ancestry-path
check_result 'M L J I H' G..M --ancestry-path -- file
check_result 'M L K J I H' G..M --ancestry-path --parents -- file
check_result 'M H L J I' G..M --ancestry-path --simplify-merges -- file

# B..F should be able to simplify the merge D from irrelevant side branch C.
# Default log should also be free to follow B-D, and ignore C.
# But --full-history shouldn't drop D on its own - without simplification,
# we can't decide if the merge from INTERESTING commit C was sensible.
check_result 'F D C' B..F
check_result 'F' B..F -- file
check_result '(B)F' B..F --parents -- file
check_result 'F D' B..F --full-history -- file
check_result '(D)F (BA)D' B..F --full-history --parents -- file
check_result '(B)F' B..F --simplify-merges -- file
check_result 'F D' B..F --ancestry-path
check_result 'F' B..F --ancestry-path -- file
check_result 'F' B..F --ancestry-path --parents -- file
check_result 'F' B..F --ancestry-path --simplify-merges -- file
check_result 'F D' B..F --first-parent
check_result 'F' B..F --first-parent -- file

# E...F should be equivalent to E F ^B, and be able to drop D as above.
check_result 'F' E F ^B -- file # includes D
check_result 'F' E...F -- file # includes D

# Any sort of full history of C..F should show D, as it's the connection to C,
# and it differs from it.
check_result 'F D B' C..F
check_result 'F B' C..F -- file
check_result '(B)F (A)B' C..F --parents -- file
check_result 'F D B' C..F --full-history -- file
check_result '(D)F (BC)D (A)B' C..F --full-history --parents -- file
check_result '(D)F (BC)D (A)B' C..F --simplify-merges -- file
check_result 'F D' C..F --ancestry-path
check_result 'F D' C..F --ancestry-path -- file
check_result 'F D' C..F --ancestry-path --parents -- file
check_result 'F D' C..F --ancestry-path --simplify-merges -- file
check_result 'F D B' C..F --first-parent
check_result 'F B' C..F --first-parent -- file


test_done
