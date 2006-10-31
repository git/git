#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='See why rewinding head breaks send-pack

'
. ./test-lib.sh

touch cpio-test
test_expect_success 'working cpio' 'echo cpio-test | cpio -o > /dev/null'

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
	git-update-ref HEAD "$commit" &&
	git-clone -l ./. victim &&
	cd victim &&
	git-log &&
	cd .. &&
	git-update-ref HEAD "$zero" &&
	parent=$zero &&
	for i in $cnt
	do
	    sleep 1 &&
	    commit=$(echo "Rebase #$i" | git-commit-tree $tree -p $parent) &&
	    parent=$commit || return 1
	done &&
	git-update-ref HEAD "$commit" &&
	echo Rebase &&
	git-log'

test_expect_success \
        'pushing rewound head should not barf but require --force' ' 
	# should not fail but refuse to update.
	if git-send-pack ./victim/.git/ master
	then
		# now it should fail with Pasky patch
		echo >&2 Gaah, it should have failed.
		false
	else
		echo >&2 Thanks, it correctly failed.
		true
	fi &&
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

unset GIT_CONFIG GIT_CONFIG_LOCAL
HOME=`pwd`/no-such-directory
export HOME ;# this way we force the victim/.git/config to be used.

test_expect_success \
        'pushing with --force should be denied with denyNonFastforwards' '
	cd victim &&
	git-repo-config receive.denyNonFastforwards true &&
	cd .. &&
	git-update-ref refs/heads/master master^ &&
	git-send-pack --force ./victim/.git/ master &&
	! diff -u .git/refs/heads/master victim/.git/refs/heads/master
'

test_done
