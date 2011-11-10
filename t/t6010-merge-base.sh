#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Merge base computation.
'

. ./test-lib.sh

T=$(git write-tree)

M=1130000000
Z=+0000

GIT_COMMITTER_EMAIL=git@comm.iter.xz
GIT_COMMITTER_NAME='C O Mmiter'
GIT_AUTHOR_NAME='A U Thor'
GIT_AUTHOR_EMAIL=git@au.thor.xz
export GIT_COMMITTER_EMAIL GIT_COMMITTER_NAME GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL

doit() {
	OFFSET=$1; shift
	NAME=$1; shift
	PARENTS=
	for P
	do
		PARENTS="${PARENTS}-p $P "
	done
	GIT_COMMITTER_DATE="$(($M + $OFFSET)) $Z"
	GIT_AUTHOR_DATE=$GIT_COMMITTER_DATE
	export GIT_COMMITTER_DATE GIT_AUTHOR_DATE
	commit=$(echo $NAME | git commit-tree $T $PARENTS)
	echo $commit >.git/refs/tags/$NAME
	echo $commit
}

#  E---D---C---B---A
#  \'-_         \   \
#   \  `---------G   \
#    \                \
#     F----------------H

# Setup...
E=$(doit 5 E)
D=$(doit 4 D $E)
F=$(doit 6 F $E)
C=$(doit 3 C $D)
B=$(doit 2 B $C)
A=$(doit 1 A $B)
G=$(doit 7 G $B $E)
H=$(doit 8 H $A $F)

test_expect_success 'compute merge-base (single)' \
    'MB=$(git merge-base G H) &&
     expr "$(git name-rev "$MB")" : "[0-9a-f]* tags/B"'

test_expect_success 'compute merge-base (all)' \
    'MB=$(git merge-base --all G H) &&
     expr "$(git name-rev "$MB")" : "[0-9a-f]* tags/B"'

test_expect_success 'compute merge-base with show-branch' \
    'MB=$(git show-branch --merge-base G H) &&
     expr "$(git name-rev "$MB")" : "[0-9a-f]* tags/B"'

# Setup for second test to demonstrate that relying on timestamps in a
# distributed SCM to provide a _consistent_ partial ordering of commits
# leads to insanity.
#
#               Relative
# Structure     timestamps
#
#   PL  PR        +4  +4
#  /  \/  \      /  \/  \
# L2  C2  R2    +3  -1  +3
# |   |   |     |   |   |
# L1  C1  R1    +2  -2  +2
# |   |   |     |   |   |
# L0  C0  R0    +1  -3  +1
#   \ |  /        \ |  /
#     S             0
#
# The left and right chains of commits can be of any length and complexity as
# long as all of the timestamps are greater than that of S.

S=$(doit  0 S)

C0=$(doit -3 C0 $S)
C1=$(doit -2 C1 $C0)
C2=$(doit -1 C2 $C1)

L0=$(doit  1 L0 $S)
L1=$(doit  2 L1 $L0)
L2=$(doit  3 L2 $L1)

R0=$(doit  1 R0 $S)
R1=$(doit  2 R1 $R0)
R2=$(doit  3 R2 $R1)

PL=$(doit  4 PL $L2 $C2)
PR=$(doit  4 PR $C2 $R2)

test_expect_success 'compute merge-base (single)' \
    'MB=$(git merge-base PL PR) &&
     expr "$(git name-rev "$MB")" : "[0-9a-f]* tags/C2"'

test_expect_success 'compute merge-base (all)' \
    'MB=$(git merge-base --all PL PR) &&
     expr "$(git name-rev "$MB")" : "[0-9a-f]* tags/C2"'

# Another set to demonstrate base between one commit and a merge
# in the documentation.
#
# * C (MMC) * B (MMB) * A  (MMA)
# * o       * o       * o
# * o       * o       * o
# * o       * o       * o
# * o       | _______/
# |         |/
# |         * 1 (MM1)
# | _______/
# |/
# * root (MMR)


test_expect_success 'merge-base for octopus-step (setup)' '
	test_tick && git commit --allow-empty -m root && git tag MMR &&
	test_tick && git commit --allow-empty -m 1 && git tag MM1 &&
	test_tick && git commit --allow-empty -m o &&
	test_tick && git commit --allow-empty -m o &&
	test_tick && git commit --allow-empty -m o &&
	test_tick && git commit --allow-empty -m A && git tag MMA &&
	git checkout MM1 &&
	test_tick && git commit --allow-empty -m o &&
	test_tick && git commit --allow-empty -m o &&
	test_tick && git commit --allow-empty -m o &&
	test_tick && git commit --allow-empty -m B && git tag MMB &&
	git checkout MMR &&
	test_tick && git commit --allow-empty -m o &&
	test_tick && git commit --allow-empty -m o &&
	test_tick && git commit --allow-empty -m o &&
	test_tick && git commit --allow-empty -m o &&
	test_tick && git commit --allow-empty -m C && git tag MMC
'

test_expect_success 'merge-base A B C' '
	MB=$(git merge-base --all MMA MMB MMC) &&
	MM1=$(git rev-parse --verify MM1) &&
	test "$MM1" = "$MB"
'

test_expect_success 'merge-base A B C using show-branch' '
	MB=$(git show-branch --merge-base MMA MMB MMC) &&
	MMR=$(git rev-parse --verify MMR) &&
	test "$MMR" = "$MB"
'

test_expect_success 'criss-cross merge-base for octopus-step (setup)' '
	git reset --hard MMR &&
	test_tick && git commit --allow-empty -m 1 && git tag CC1 &&
	git reset --hard E &&
	test_tick && git commit --allow-empty -m 2 && git tag CC2 &&
	test_tick && git merge -s ours CC1 &&
	test_tick && git commit --allow-empty -m o &&
	test_tick && git commit --allow-empty -m B && git tag CCB &&
	git reset --hard CC1 &&
	test_tick && git merge -s ours CC2 &&
	test_tick && git commit --allow-empty -m A && git tag CCA
'

test_expect_success 'merge-base B A^^ A^^2' '
	MB0=$(git merge-base --all CCB CCA^^ CCA^^2 | sort) &&
	MB1=$(git rev-parse CC1 CC2 | sort) &&
	test "$MB0" = "$MB1"
'

test_done
