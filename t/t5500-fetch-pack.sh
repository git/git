#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

test_description='Testing multi_ack pack fetching

'
. ./test-lib.sh

# Test fetch-pack/upload-pack pair.

# Some convenience functions

function show_count () {
	commit_count=$(($commit_count+1))
	printf "      %d\r" $commit_count
}

function add () {
	local name=$1
	local text="$@"
	local branch=${name:0:1}
	local parents=""

	shift
	while test $1; do
		parents="$parents -p $1"
		shift
	done

	echo "$text" > test.txt
	git-update-index --add test.txt
	tree=$(git-write-tree)
	# make sure timestamps are in correct order
	sec=$(($sec+1))
	commit=$(echo "$text" | GIT_AUTHOR_DATE=$sec \
		git-commit-tree $tree $parents 2>>log2.txt)
	export $name=$commit
	echo $commit > .git/refs/heads/$branch
	eval ${branch}TIP=$commit
}

function count_objects () {
	ls .git/objects/??/* 2>>log2.txt | wc -l | tr -d " "
}

function test_expect_object_count () {
	local message=$1
	local count=$2

	output="$(count_objects)"
	test_expect_success \
		"new object count $message" \
		"test $count = $output"
}

function test_repack () {
	local rep=$1

	test_expect_success "repack && prune-packed in $rep" \
		'(git-repack && git-prune-packed)2>>log.txt'
}

function pull_to_client () {
	local number=$1
	local heads=$2
	local count=$3
	local no_strict_count_check=$4

	cd client
	test_expect_success "$number pull" \
		"git-fetch-pack -v .. $heads > log.txt 2>&1"
	case "$heads" in *A*) echo $ATIP > .git/refs/heads/A;; esac
	case "$heads" in *B*) echo $BTIP > .git/refs/heads/B;; esac
	git-symbolic-ref HEAD refs/heads/${heads:0:1}
	test_expect_success "fsck" 'git-fsck-objects --full > fsck.txt 2>&1'
	test_expect_object_count "after $number pull" $count
	pack_count=$(grep Packing log.txt|tr -dc "0-9")
	test -z "$pack_count" && pack_count=0
	if [ -z "$no_strict_count_check" ]; then
		test_expect_success "minimal count" "test $count = $pack_count"
	else
		test $count != $pack_count && \
			echo "WARNING: $pack_count objects transmitted, only $count of which were needed"
	fi
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
	git-init-db 2>> log2.txt
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
git-symbolic-ref HEAD refs/heads/B

pull_to_client 1st "B A" $((11*3))

(cd client; test_repack client)

add A11 $A10

prev=1; cur=2; while [ $cur -le 65 ]; do
	add B$cur $(eval echo \$B$prev)
	prev=$cur
	cur=$(($cur+1))
done

pull_to_client 2nd "B" $((64*3))

(cd client; test_repack client)

pull_to_client 3rd "A" $((1*3)) # old fails

test_done
