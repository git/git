#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Merge base and parent list computation.
'

. ./test-lib.sh

M=1130000000
Z=+0000

GIT_COMMITTER_EMAIL=git@comm.iter.xz
GIT_COMMITTER_NAME='C O Mmiter'
GIT_AUTHOR_NAME='A U Thor'
GIT_AUTHOR_EMAIL=git@au.thor.xz
export GIT_COMMITTER_EMAIL GIT_COMMITTER_NAME GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL

doit () {
	OFFSET=$1 &&
	NAME=$2 &&
	shift 2 &&

	PARENTS= &&
	for P
	do
		PARENTS="${PARENTS}-p $P "
	done &&

	GIT_COMMITTER_DATE="$(($M + $OFFSET)) $Z" &&
	GIT_AUTHOR_DATE=$GIT_COMMITTER_DATE &&
	export GIT_COMMITTER_DATE GIT_AUTHOR_DATE &&

	commit=$(echo $NAME | git commit-tree $T $PARENTS) &&

	echo $commit >.git/refs/tags/$NAME &&
	echo $commit
}

test_expect_success 'setup' '
	T=$(git mktree </dev/null)
'

test_expect_success 'set up G and H' '
	# E---D---C---B---A
	# \"-_         \   \
	#  \  `---------G   \
	#   \                \
	#    F----------------H
	E=$(doit 5 E) &&
	D=$(doit 4 D $E) &&
	F=$(doit 6 F $E) &&
	C=$(doit 3 C $D) &&
	B=$(doit 2 B $C) &&
	A=$(doit 1 A $B) &&
	G=$(doit 7 G $B $E) &&
	H=$(doit 8 H $A $F)
'

test_expect_success 'merge-base G H' '
	git name-rev $B >expected &&

	MB=$(git merge-base G H) &&
	git name-rev "$MB" >actual.single &&

	MB=$(git merge-base --all G H) &&
	git name-rev "$MB" >actual.all &&

	MB=$(git show-branch --merge-base G H) &&
	git name-rev "$MB" >actual.sb &&

	test_cmp expected actual.single &&
	test_cmp expected actual.all &&
	test_cmp expected actual.sb
'

test_expect_success 'merge-base/show-branch --independent' '
	git name-rev "$H" >expected1 &&
	git name-rev "$H" "$G" >expected2 &&

	parents=$(git merge-base --independent H) &&
	git name-rev $parents >actual1.mb &&
	parents=$(git merge-base --independent A H G) &&
	git name-rev $parents >actual2.mb &&

	parents=$(git show-branch --independent H) &&
	git name-rev $parents >actual1.sb &&
	parents=$(git show-branch --independent A H G) &&
	git name-rev $parents >actual2.sb &&

	test_cmp expected1 actual1.mb &&
	test_cmp expected2 actual2.mb &&
	test_cmp expected1 actual1.sb &&
	test_cmp expected2 actual2.sb
'

test_expect_success 'unsynchronized clocks' '
	# This test is to demonstrate that relying on timestamps in a distributed
	# SCM to provide a _consistent_ partial ordering of commits leads to
	# insanity.
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

	S=$(doit  0 S) &&

	C0=$(doit -3 C0 $S) &&
	C1=$(doit -2 C1 $C0) &&
	C2=$(doit -1 C2 $C1) &&

	L0=$(doit  1 L0 $S) &&
	L1=$(doit  2 L1 $L0) &&
	L2=$(doit  3 L2 $L1) &&

	R0=$(doit  1 R0 $S) &&
	R1=$(doit  2 R1 $R0) &&
	R2=$(doit  3 R2 $R1) &&

	PL=$(doit  4 PL $L2 $C2) &&
	PR=$(doit  4 PR $C2 $R2) &&

	git name-rev $C2 >expected &&

	MB=$(git merge-base PL PR) &&
	git name-rev "$MB" >actual.single &&

	MB=$(git merge-base --all PL PR) &&
	git name-rev "$MB" >actual.all &&

	test_cmp expected actual.single &&
	test_cmp expected actual.all
'

test_expect_success '--independent with unsynchronized clocks' '
	IB=$(doit 0 IB) &&
	I1=$(doit -10 I1 $IB) &&
	I2=$(doit  -9 I2 $I1) &&
	I3=$(doit  -8 I3 $I2) &&
	I4=$(doit  -7 I4 $I3) &&
	I5=$(doit  -6 I5 $I4) &&
	I6=$(doit  -5 I6 $I5) &&
	I7=$(doit  -4 I7 $I6) &&
	I8=$(doit  -3 I8 $I7) &&
	IH=$(doit  -2 IH $I8) &&

	echo $IH >expected &&
	git merge-base --independent IB IH >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-base for octopus-step (setup)' '
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

	test_commit MMR &&
	test_commit MM1 &&
	test_commit MM-o &&
	test_commit MM-p &&
	test_commit MM-q &&
	test_commit MMA &&
	git checkout MM1 &&
	test_commit MM-r &&
	test_commit MM-s &&
	test_commit MM-t &&
	test_commit MMB &&
	git checkout MMR &&
	test_commit MM-u &&
	test_commit MM-v &&
	test_commit MM-w &&
	test_commit MM-x &&
	test_commit MMC
'

test_expect_success 'merge-base A B C' '
	git rev-parse --verify MM1 >expected &&
	git rev-parse --verify MMR >expected.sb &&

	git merge-base --all MMA MMB MMC >actual &&
	git merge-base --all --octopus MMA MMB MMC >actual.common &&
	git show-branch --merge-base MMA MMB MMC >actual.sb &&

	test_cmp expected actual &&
	test_cmp expected.sb actual.common &&
	test_cmp expected.sb actual.sb
'

test_expect_success 'criss-cross merge-base for octopus-step' '
	git reset --hard MMR &&
	test_commit CC1 &&
	git reset --hard E &&
	test_commit CC2 &&
	test_tick &&
	# E is a root commit unrelated to MMR root on which CC1 is based
	git merge -s ours --allow-unrelated-histories CC1 &&
	test_commit CC-o &&
	test_commit CCB &&
	git reset --hard CC1 &&
	# E is a root commit unrelated to MMR root on which CC1 is based
	git merge -s ours --allow-unrelated-histories CC2 &&
	test_commit CCA &&

	git rev-parse CC1 CC2 >expected &&
	git merge-base --all CCB CCA^^ CCA^^2 >actual &&

	sort expected >expected.sorted &&
	sort actual >actual.sorted &&
	test_cmp expected.sorted actual.sorted
'

test_expect_success 'using reflog to find the fork point' '
	git reset --hard &&
	git checkout -b base $E &&

	(
		for count in 1 2 3
		do
			git commit --allow-empty -m "Base commit #$count" &&
			git rev-parse HEAD >expect$count &&
			git checkout -B derived &&
			git commit --allow-empty -m "Derived #$count" &&
			git rev-parse HEAD >derived$count &&
			git checkout -B base $E || exit 1
		done

		for count in 1 2 3
		do
			git merge-base --fork-point base $(cat derived$count) >actual &&
			test_cmp expect$count actual || exit 1
		done

	) &&
	# check that we correctly default to HEAD
	git checkout derived &&
	git merge-base --fork-point base >actual &&
	test_cmp expect3 actual
'

test_expect_success '--fork-point works with empty reflog' '
	git -c core.logallrefupdates=false branch no-reflog base &&
	git rev-parse base >expect &&
	git merge-base --fork-point no-reflog derived >actual &&
	test_cmp expect actual
'

test_expect_success '--fork-point works with merge-base outside reflog' '
	git -c core.logallrefupdates=false checkout no-reflog &&
	git -c core.logallrefupdates=false commit --allow-empty -m "Commit outside reflogs" &&
	git rev-parse base >expect &&
	git merge-base --fork-point no-reflog derived >actual &&
	test_cmp expect actual
'

test_expect_success '--fork-point works with merge-base outside partial reflog' '
	git -c core.logallrefupdates=true branch partial-reflog base &&
	git rev-parse no-reflog >.git/refs/heads/partial-reflog &&
	git rev-parse no-reflog >expect &&
	git merge-base --fork-point partial-reflog no-reflog >actual &&
	test_cmp expect actual
'

test_expect_success 'merge-base --octopus --all for complex tree' '
	# Best common ancestor for JE, JAA and JDD is JC
	#             JE
	#            / |
	#           /  |
	#          /   |
	#  JAA    /    |
	#   |\   /     |
	#   | \  | JDD |
	#   |  \ |/ |  |
	#   |   JC JD  |
	#   |    | /|  |
	#   |    |/ |  |
	#  JA    |  |  |
	#   |\  /|  |  |
	#   X JB |  X  X
	#   \  \ | /   /
	#    \__\|/___/
	#        J
	test_commit J &&
	test_commit JB &&
	git reset --hard J &&
	test_commit JC &&
	git reset --hard J &&
	test_commit JTEMP1 &&
	test_merge JA JB &&
	test_merge JAA JC &&
	git reset --hard J &&
	test_commit JTEMP2 &&
	test_merge JD JB &&
	test_merge JDD JC &&
	git reset --hard J &&
	test_commit JTEMP3 &&
	test_merge JE JC &&
	git rev-parse JC >expected &&
	git merge-base --all --octopus JAA JDD JE >actual &&
	test_cmp expected actual
'

test_done
