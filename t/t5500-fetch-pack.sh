#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

test_description='Testing multi_ack pack fetching

'
. ./test-lib.sh

# Test fetch-pack/upload-pack pair.

# Some convenience functions

add () {
	name=$1
	text="$@"
	branch=`echo $name | sed -e 's/^\(.\).*$/\1/'`
	parents=""

	shift
	while test $1; do
		parents="$parents -p $1"
		shift
	done

	echo "$text" > test.txt
	git update-index --add test.txt
	tree=$(git write-tree)
	# make sure timestamps are in correct order
	sec=$(($sec+1))
	commit=$(echo "$text" | GIT_AUTHOR_DATE=$sec \
		git commit-tree $tree $parents 2>>log2.txt)
	export $name=$commit
	echo $commit > .git/refs/heads/$branch
	eval ${branch}TIP=$commit
}

count_objects () {
	ls .git/objects/??/* 2>>log2.txt | wc -l | tr -d " "
}

test_expect_object_count () {
	message=$1
	count=$2

	output="$(count_objects)"
	test_expect_success \
		"new object count $message" \
		"test $count = $output"
}

pull_to_client () {
	number=$1
	heads=$2
	count=$3
	no_strict_count_check=$4

	cd client
	test_expect_success "$number pull" \
		"git-fetch-pack -k -v .. $heads"
	case "$heads" in *A*) echo $ATIP > .git/refs/heads/A;; esac
	case "$heads" in *B*) echo $BTIP > .git/refs/heads/B;; esac
	git symbolic-ref HEAD refs/heads/`echo $heads | sed -e 's/^\(.\).*$/\1/'`

	test_expect_success "fsck" 'git fsck --full > fsck.txt 2>&1'

	test_expect_success 'check downloaded results' \
	'mv .git/objects/pack/pack-* . &&
	 p=`ls -1 pack-*.pack` &&
	 git unpack-objects <$p &&
	 git fsck --full'

	test_expect_success "new object count after $number pull" \
	'idx=`echo pack-*.idx` &&
	 pack_count=`git show-index <$idx | wc -l` &&
	 test $pack_count = $count'
	test -z "$pack_count" && pack_count=0
	if [ -z "$no_strict_count_check" ]; then
		test_expect_success "minimal count" "test $count = $pack_count"
	else
		test $count != $pack_count && \
			echo "WARNING: $pack_count objects transmitted, only $count of which were needed"
	fi
	rm -f pack-*
	cd ..
}

# Here begins the actual testing

# A1 - ... - A20 - A21
#    \
#      B1  -   B2 - .. - B70

# client pulls A20, B1. Then tracks only B. Then pulls A.

(
	mkdir client &&
	cd client &&
	git init 2>> log2.txt &&
	git config transfer.unpacklimit 0
)

add A1

prev=1; cur=2; while [ $cur -le 10 ]; do
	add A$cur $(eval echo \$A$prev)
	prev=$cur
	cur=$(($cur+1))
done

add B1 $A1

echo $ATIP > .git/refs/heads/A
echo $BTIP > .git/refs/heads/B
git symbolic-ref HEAD refs/heads/B

pull_to_client 1st "B A" $((11*3))

add A11 $A10

prev=1; cur=2; while [ $cur -le 65 ]; do
	add B$cur $(eval echo \$B$prev)
	prev=$cur
	cur=$(($cur+1))
done

pull_to_client 2nd "B" $((64*3))

pull_to_client 3rd "A" $((1*3)) # old fails

test_expect_success "clone shallow" "git-clone --depth 2 file://`pwd`/. shallow"

(cd shallow; git count-objects -v) > count.shallow

test_expect_success "clone shallow object count" \
	"test \"in-pack: 18\" = \"$(grep in-pack count.shallow)\""

count_output () {
	sed -e '/^in-pack:/d' -e '/^packs:/d' -e '/: 0$/d' "$1"
}

test_expect_success "clone shallow object count (part 2)" '
	test -z "$(count_output count.shallow)"
'

test_expect_success "fsck in shallow repo" \
	"(cd shallow; git fsck --full)"

#test_done; exit

add B66 $B65
add B67 $B66

test_expect_success "pull in shallow repo" \
	"(cd shallow; git pull .. B)"

(cd shallow; git count-objects -v) > count.shallow
test_expect_success "clone shallow object count" \
	"test \"count: 6\" = \"$(grep count count.shallow)\""

add B68 $B67
add B69 $B68

test_expect_success "deepening pull in shallow repo" \
	"(cd shallow; git pull --depth 4 .. B)"

(cd shallow; git count-objects -v) > count.shallow
test_expect_success "clone shallow object count" \
	"test \"count: 12\" = \"$(grep count count.shallow)\""

test_expect_success "deepening fetch in shallow repo" \
	"(cd shallow; git fetch --depth 4 .. A:A)"

(cd shallow; git count-objects -v) > count.shallow
test_expect_success "clone shallow object count" \
	"test \"count: 18\" = \"$(grep count count.shallow)\""

test_expect_failure "pull in shallow repo with missing merge base" \
	"(cd shallow; git pull --depth 4 .. A)"

test_done
