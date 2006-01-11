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

test_done
