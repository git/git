#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='See why rewinding head breaks send-pack

'
. ./test-lib.sh

cnt=64
test_expect_success setup '
	test_tick &&
	mkdir mozart mozart/is &&
	echo "Commit #0" >mozart/is/pink &&
	git-update-index --add mozart/is/pink &&
	tree=$(git-write-tree) &&
	commit=$(echo "Commit #0" | git-commit-tree $tree) &&
	zero=$commit &&
	parent=$zero &&
	i=0 &&
	while test $i -le $cnt
	do
	    i=$(($i+1)) &&
	    test_tick &&
	    echo "Commit #$i" >mozart/is/pink &&
	    git-update-index --add mozart/is/pink &&
	    tree=$(git-write-tree) &&
	    commit=$(echo "Commit #$i" | git-commit-tree $tree -p $parent) &&
	    git-update-ref refs/tags/commit$i $commit &&
	    parent=$commit || return 1
	done &&
	git-update-ref HEAD "$commit" &&
	git-clone ./. victim &&
	cd victim &&
	git-log &&
	cd .. &&
	git-update-ref HEAD "$zero" &&
	parent=$zero &&
	i=0 &&
	while test $i -le $cnt
	do
	    i=$(($i+1)) &&
	    test_tick &&
	    echo "Rebase #$i" >mozart/is/pink &&
	    git-update-index --add mozart/is/pink &&
	    tree=$(git-write-tree) &&
	    commit=$(echo "Rebase #$i" | git-commit-tree $tree -p $parent) &&
	    git-update-ref refs/tags/rebase$i $commit &&
	    parent=$commit || return 1
	done &&
	git-update-ref HEAD "$commit" &&
	echo Rebase &&
	git-log'

test_expect_success 'pack the source repository' '
	git repack -a -d &&
	git prune
'

test_expect_success 'pack the destination repository' '
	cd victim &&
	git repack -a -d &&
	git prune &&
	cd ..
'

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

test_expect_success \
        'push can be used to delete a ref' '
	cd victim &&
	git branch extra master &&
	cd .. &&
	test -f victim/.git/refs/heads/extra &&
	git-send-pack ./victim/.git/ :extra master &&
	! test -f victim/.git/refs/heads/extra
'

unset GIT_CONFIG GIT_CONFIG_LOCAL
HOME=`pwd`/no-such-directory
export HOME ;# this way we force the victim/.git/config to be used.

test_expect_success \
        'pushing with --force should be denied with denyNonFastforwards' '
	cd victim &&
	git-config receive.denyNonFastforwards true &&
	cd .. &&
	git-update-ref refs/heads/master master^ || return 1
	git-send-pack --force ./victim/.git/ master && return 1
	! diff .git/refs/heads/master victim/.git/refs/heads/master
'

test_done
