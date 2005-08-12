#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='See why rewinding head breaks send-pack

'
. ./test-lib.sh

cnt='1'
test_expect_success setup '
	tree=$(git-write-tree) &&
	commit=$(echo "Commit #0" | git-commit-tree $tree) &&
	zero=$commit &&
	parent=$zero &&
	for i in $cnt
	do
	    sleep 1 &&
	    commit=$(echo "Commit #$i" | git-commit-tree $tree -p $parent) &&
	    parent=$commit || return 1
	done &&
	echo "$commit" >.git/HEAD &&
	git clone -l ./. victim &&
	cd victim &&
	git log &&
	cd .. &&
	echo $zero >.git/HEAD &&
	parent=$zero &&
	for i in $cnt
	do
	    sleep 1 &&
	    commit=$(echo "Rebase #$i" | git-commit-tree $tree -p $parent) &&
	    parent=$commit || return 1
	done &&
	echo "$commit" >.git/HEAD &&
	echo Rebase &&
	git log'

test_expect_success \
        'pushing rewound head should not barf but require --force' ' 
	# should not fail but refuse to update.
	git-send-pack ./victim/.git/ master &&
	if cmp victim/.git/refs/heads/master .git/refs/heads/master
	then
		# should have been left as it was!
		false
	else
		true
	fi &&
	# this should update
	git-send-pack --force ./victim/.git/ master &&
	cmp victim/.git/refs/heads/master .git/refs/heads/master
'

test_done
