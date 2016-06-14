#!/bin/sh
#
# Copyright (c) 2016 Stephan Beyer
#
test_description='Tests git bisect algorithm'

. ./test-lib.sh

test_expect_success 'set up a history for the test' '
	test_commit A1 A 1 &&
	test_commit A2 A 2 &&
	test_commit A3 A 3 &&
	test_commit A4 A 4 &&
	test_commit A5 A 5 &&
	test_commit A6 A 6 &&
	git checkout -b b A5 &&
	test_commit B1 B 1 &&
	git checkout master &&
	test_commit A7 A 7 &&
	git checkout b &&
	test_commit B2 B 2 &&
	git checkout master &&
	test_commit A8 A 8 &&
	test_merge Bmerge b &&
	git checkout b &&
	test_commit B3 B 3 &&
	git checkout -b c A7 &&
	test_commit C1 C 1 &&
	git checkout -b d A3 &&
	test_commit D1 D 1 &&
	git checkout c &&
	test_commit C2 C 2 &&
	git checkout d &&
	test_commit D2 D 2 &&
	git checkout c &&
	test_commit C3 C 3 &&
	git checkout master &&
	git merge -m BCDmerge b c d &&
	git tag BCDmerge &&
	test_commit A9 A 9 &&
	git checkout d &&
	test_commit D3 &&
	git checkout master
'

test_expect_success 'bisect algorithm works in linear history with an odd number of commits' '
	git bisect start A7 &&
	git bisect next &&
	test_cmp_rev HEAD A3 A4
'

test_expect_success 'bisect algorithm works in linear history with an even number of commits' '
	git bisect reset &&
	git bisect start A8 &&
	git bisect next &&
	test_cmp_rev HEAD A4
'

test_expect_success 'bisect algorithm works with a merge' '
	git bisect reset &&
	git bisect start Bmerge &&
	git bisect next &&
	test_cmp_rev HEAD A5 &&
	git bisect good &&
	test_cmp_rev HEAD A8 &&
	git bisect good &&
	test_cmp_rev HEAD B1 B2
'

#                   | w  min | w  min | w  min | w  min |
# B---.    BCDmerge | 18  0  | 9    0 | 5    0 | 3    0 |
# |\ \ \            |        |        |        |        |
# | | | *  D2       | 5   5  | 2    2 | 2    2*| good   |
# | | | *  D1       | 4   4  | 1    1 | 1    1 | good   |
# | | * |  C3       | 10  8  | 1    1 | 1    1 | 1    1*|
# | | * |  C2       | 9   9 *| good   | good   | good   |
# | | * |  C1       | 8   8  | good   | good   | good   |
# | * | |  B3       | 8   8  | 3    3 | 1    1 | 1    1*|
# * | | |  Bmerge   | 11  7  | 4    4*| good   | good   |
# |\ \ \ \          |        |        |        |        |
# | |/ / /          |        |        |        |        |
# | * | |  B2       | 7   7  | 2    2 | good   | good   |
# | * | |  B1       | 6   6  | 1    1 | good   | good   |
# * | | |  A8       | 8   8  | 1    1 | good   | good   |
# | |/ /            |        |        |        |        |
# |/| |             |        |        |        |        |
# * | |   A7        | 7   7  | good   | good   | good   |
# * | |   A6        | 6   6  | good   | good   | good   |
# |/ /              |        |        |        |        |
# * |     A5        | 5   5  | good   | good   | good   |
# * |     A4        | 4   4  | good   | good   | good   |
# |/                |        |        |        |        |
# *       A3        | 3   3  | good   | good   | good   |
# *       A2        | 2   2  | good   | good   | good   |
# *       A1        | 1   1  | good   | good   | good   |

test_expect_success 'bisect algorithm works with octopus merge' '
	git bisect reset &&
	git bisect start BCDmerge &&
	git bisect next &&
	test_cmp_rev HEAD C2 &&
	git bisect good &&
	test_cmp_rev HEAD Bmerge &&
	git bisect good &&
	test_cmp_rev HEAD D2 &&
	git bisect good &&
	test_cmp_rev HEAD B3 C3 &&
	git bisect good &&
	test_cmp_rev HEAD C3 B3 &&
	git bisect good > output &&
	grep "$(git rev-parse BCDmerge) is the first bad commit" output
'

# G 5a6bcdf        D3       | w  min | w  min |
# | B 02f2eed      A9       | 14  0  | 7   0  |
# | *---. 6174c5c  BCDmerge | 13  1  | 6   1  |
# | |\ \ \                  |        |        |
# | |_|_|/                  |        |        |
# |/| | |                   |        |        |
# G | | | a6d6dab  D2       | good   | good   |
# * | | | 86414e4  D1       | good   | good   |
# | | | * c672402  C3       | 7   7 *| good   |
# | | | * 0555272  C2       | 6   6  | good   |
# | | | * 28c2b2a  C1       | 5   5  | good   |
# | | * | 4b5a7d9  B3       | 5   5  | 3   3 *|
# | * | | a419ab7  Bmerge   | 8   6  | 4   3 *|
# | |\ \ \                  |        |        |
# | | |/ /                  |        |        |
# | | * | 4fa1e39  B2       | 4   4  | 2   2  |
# | | * | 92a014d  B1       | 3   3  | 1   1  |
# | * | | 79158c7  A8       | 5   5  | 1   1  |
# | | |/                    |        |        |
# | |/|                     |        |        |
# | * | 237eb73    A7       | 4   4  | good   |
# | * | 3b2f811    A6       | 3   3  | good   |
# | |/                      |        |        |
# | * 0f2b6d2      A5       | 2   2  | good   |
# | * 1fcdaf0      A4       | 1   1  | good   |
# |/                        |        |        |
# * 096648b        A3       | good   | good   |
# * 1cf01b8        A2       | good   | good   |
# * 6623165        A1       | good   | good   |

test_expect_success 'bisect algorithm works with good commit on unrelated branch' '
	git bisect reset &&
	git bisect start A9 D3 &&
	test_cmp_rev HEAD "$(git merge-base A9 D3)" &&
	test_cmp_rev HEAD D2 &&
	git bisect good &&
	test_cmp_rev HEAD C3 &&
	git bisect good &&
	test_cmp_rev HEAD B3 Bmerge
'

test_done
