#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Merge base computation.
'

. ./test-lib.sh

T=$(git-write-tree)

M=1130000000
Z=+0000

export GIT_COMMITTER_EMAIL=git@comm.iter.xz
export GIT_COMMITTER_NAME='C O Mmiter'
export GIT_AUTHOR_NAME='A U Thor'
export GIT_AUTHOR_EMAIL=git@au.thor.xz

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
	commit=$(echo $NAME | git-commit-tree $T $PARENTS)
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
    'MB=$(git-merge-base G H) &&
     expr "$(git-name-rev "$MB")" : "[0-9a-f]* tags/B"'

test_expect_success 'compute merge-base (all)' \
    'MB=$(git-merge-base --all G H) &&
     expr "$(git-name-rev "$MB")" : "[0-9a-f]* tags/B"'

test_expect_success 'compute merge-base with show-branch' \
    'MB=$(git-show-branch --merge-base G H) &&
     expr "$(git-name-rev "$MB")" : "[0-9a-f]* tags/B"'

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
    'MB=$(git-merge-base PL PR) &&
     expr "$(git-name-rev "$MB")" : "[0-9a-f]* tags/C2"'

test_expect_success 'compute merge-base (all)' \
    'MB=$(git-merge-base --all PL PR) &&
     expr "$(git-name-rev "$MB")" : "[0-9a-f]* tags/C2"'

test_done
