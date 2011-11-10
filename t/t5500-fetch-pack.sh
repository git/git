#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

test_description='Testing multi_ack pack fetching'

. ./test-lib.sh

# Test fetch-pack/upload-pack pair.

# Some convenience functions

add () {
	name=$1 &&
	text="$@" &&
	branch=`echo $name | sed -e 's/^\(.\).*$/\1/'` &&
	parents="" &&

	shift &&
	while test $1; do
		parents="$parents -p $1" &&
		shift
	done &&

	echo "$text" > test.txt &&
	git update-index --add test.txt &&
	tree=$(git write-tree) &&
	# make sure timestamps are in correct order
	test_tick &&
	commit=$(echo "$text" | git commit-tree $tree $parents) &&
	eval "$name=$commit; export $name" &&
	echo $commit > .git/refs/heads/$branch &&
	eval ${branch}TIP=$commit
}

pull_to_client () {
	number=$1 &&
	heads=$2 &&
	count=$3 &&
	test_expect_success "$number pull" '
		(
			cd client &&
			git fetch-pack -k -v .. $heads &&

			case "$heads" in
			    *A*)
				    echo $ATIP > .git/refs/heads/A;;
			esac &&
			case "$heads" in *B*)
			    echo $BTIP > .git/refs/heads/B;;
			esac &&
			git symbolic-ref HEAD refs/heads/`echo $heads \
				| sed -e "s/^\(.\).*$/\1/"` &&

			git fsck --full &&

			mv .git/objects/pack/pack-* . &&
			p=`ls -1 pack-*.pack` &&
			git unpack-objects <$p &&
			git fsck --full &&

			idx=`echo pack-*.idx` &&
			pack_count=`git show-index <$idx | wc -l` &&
			test $pack_count = $count &&
			rm -f pack-*
		)
	'
}

# Here begins the actual testing

# A1 - ... - A20 - A21
#    \
#      B1  -   B2 - .. - B70

# client pulls A20, B1. Then tracks only B. Then pulls A.

test_expect_success 'setup' '
	mkdir client &&
	(
		cd client &&
		git init &&
		git config transfer.unpacklimit 0
	) &&
	add A1 &&
	prev=1 &&
	cur=2 &&
	while [ $cur -le 10 ]; do
		add A$cur $(eval echo \$A$prev) &&
		prev=$cur &&
		cur=$(($cur+1))
	done &&
	add B1 $A1
	echo $ATIP > .git/refs/heads/A &&
	echo $BTIP > .git/refs/heads/B &&
	git symbolic-ref HEAD refs/heads/B
'

pull_to_client 1st "B A" $((11*3))

test_expect_success 'post 1st pull setup' '
	add A11 $A10 &&
	prev=1 &&
	cur=2 &&
	while [ $cur -le 65 ]; do
		add B$cur $(eval echo \$B$prev) &&
		prev=$cur &&
		cur=$(($cur+1))
	done
'

pull_to_client 2nd "B" $((64*3))

pull_to_client 3rd "A" $((1*3))

test_expect_success 'clone shallow' '
	git clone --depth 2 "file://$(pwd)/." shallow
'

test_expect_success 'clone shallow object count' '
	(
		cd shallow &&
		git count-objects -v
	) > count.shallow &&
	grep "^in-pack: 18" count.shallow
'

test_expect_success 'clone shallow object count (part 2)' '
	sed -e "/^in-pack:/d" -e "/^packs:/d" -e "/^size-pack:/d" \
	    -e "/: 0$/d" count.shallow > count_output &&
	! test -s count_output
'

test_expect_success 'fsck in shallow repo' '
	(
		cd shallow &&
		git fsck --full
	)
'

test_expect_success 'simple fetch in shallow repo' '
	(
		cd shallow &&
		git fetch
	)
'

test_expect_success 'no changes expected' '
	(
		cd shallow &&
		git count-objects -v
	) > count.shallow.2 &&
	cmp count.shallow count.shallow.2
'

test_expect_success 'fetch same depth in shallow repo' '
	(
		cd shallow &&
		git fetch --depth=2
	)
'

test_expect_success 'no changes expected' '
	(
		cd shallow &&
		git count-objects -v
	) > count.shallow.3 &&
	cmp count.shallow count.shallow.3
'

test_expect_success 'add two more' '
	add B66 $B65 &&
	add B67 $B66
'

test_expect_success 'pull in shallow repo' '
	(
		cd shallow &&
		git pull .. B
	)
'

test_expect_success 'clone shallow object count' '
	(
		cd shallow &&
		git count-objects -v
	) > count.shallow &&
	grep "^count: 6" count.shallow
'

test_expect_success 'add two more (part 2)' '
	add B68 $B67 &&
	add B69 $B68
'

test_expect_success 'deepening pull in shallow repo' '
	(
		cd shallow &&
		git pull --depth 4 .. B
	)
'

test_expect_success 'clone shallow object count' '
	(
		cd shallow &&
		git count-objects -v
	) > count.shallow &&
	grep "^count: 12" count.shallow
'

test_expect_success 'deepening fetch in shallow repo' '
	(
		cd shallow &&
		git fetch --depth 4 .. A:A
	)
'

test_expect_success 'clone shallow object count' '
	(
		cd shallow &&
		git count-objects -v
	) > count.shallow &&
	grep "^count: 18" count.shallow
'

test_expect_success 'pull in shallow repo with missing merge base' '
	(
		cd shallow &&
		test_must_fail git pull --depth 4 .. A
	)
'

test_expect_success 'additional simple shallow deepenings' '
	(
		cd shallow &&
		git fetch --depth=8 &&
		git fetch --depth=10 &&
		git fetch --depth=11
	)
'

test_expect_success 'clone shallow object count' '
	(
		cd shallow &&
		git count-objects -v
	) > count.shallow &&
	grep "^count: 52" count.shallow
'

test_done
