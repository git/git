#!/bin/sh
#
# Copyright (c) 2008 Johannes E. Schindelin
#

test_description='prune'
. ./test-lib.sh

test_expect_success setup '

	: > file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	git gc

'

test_expect_success 'prune stale packs' '

	orig_pack=$(echo .git/objects/pack/*.pack) &&
	: > .git/objects/tmp_1.pack &&
	: > .git/objects/tmp_2.pack &&
	test-chmtime -86501 .git/objects/tmp_1.pack &&
	git prune --expire 1.day &&
	test -f $orig_pack &&
	test -f .git/objects/tmp_2.pack &&
	! test -f .git/objects/tmp_1.pack

'

test_expect_success 'prune --expire' '

	before=$(git count-objects | sed "s/ .*//") &&
	BLOB=$(echo aleph | git hash-object -w --stdin) &&
	BLOB_FILE=.git/objects/$(echo $BLOB | sed "s/^../&\//") &&
	test $((1 + $before)) = $(git count-objects | sed "s/ .*//") &&
	test -f $BLOB_FILE &&
	git prune --expire=1.hour.ago &&
	test $((1 + $before)) = $(git count-objects | sed "s/ .*//") &&
	test -f $BLOB_FILE &&
	test-chmtime -86500 $BLOB_FILE &&
	git prune --expire 1.day &&
	test $before = $(git count-objects | sed "s/ .*//") &&
	! test -f $BLOB_FILE

'

test_expect_success 'gc: implicit prune --expire' '

	before=$(git count-objects | sed "s/ .*//") &&
	BLOB=$(echo aleph_0 | git hash-object -w --stdin) &&
	BLOB_FILE=.git/objects/$(echo $BLOB | sed "s/^../&\//") &&
	test $((1 + $before)) = $(git count-objects | sed "s/ .*//") &&
	test -f $BLOB_FILE &&
	test-chmtime -$((86400*14-30)) $BLOB_FILE &&
	git gc &&
	test $((1 + $before)) = $(git count-objects | sed "s/ .*//") &&
	test -f $BLOB_FILE &&
	test-chmtime -$((86400*14+1)) $BLOB_FILE &&
	git gc &&
	test $before = $(git count-objects | sed "s/ .*//") &&
	! test -f $BLOB_FILE

'

test_expect_success 'gc: refuse to start with invalid gc.pruneExpire' '

	git config gc.pruneExpire invalid &&
	test_must_fail git gc

'

test_expect_success 'gc: start with ok gc.pruneExpire' '

	git config gc.pruneExpire 2.days.ago &&
	git gc

'

test_expect_success 'prune: prune nonsense parameters' '

	test_must_fail git prune garbage &&
	test_must_fail git prune --- &&
	test_must_fail git prune --no-such-option

'

test_expect_success 'prune: prune unreachable heads' '

	git config core.logAllRefUpdates false &&
	mv .git/logs .git/logs.old &&
	: > file2 &&
	git add file2 &&
	git commit -m temporary &&
	tmp_head=$(git rev-list -1 HEAD) &&
	git reset HEAD^ &&
	git prune &&
	test_must_fail git reset $tmp_head --

'

test_expect_failure 'prune: do not prune heads listed as an argument' '

	: > file2 &&
	git add file2 &&
	git commit -m temporary &&
	tmp_head=$(git rev-list -1 HEAD) &&
	git reset HEAD^ &&
	git prune -- $tmp_head &&
	git reset $tmp_head --

'

test_done
