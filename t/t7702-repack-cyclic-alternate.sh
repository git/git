#!/bin/sh
#
# Copyright (c) 2014 Ephrim Khong
#

test_description='repack involving cyclic alternate'
. ./test-lib.sh

test_expect_success setup '
	GIT_OBJECT_DIRECTORY=.git//../.git/objects &&
	export GIT_OBJECT_DIRECTORY &&
	touch a &&
	git add a &&
	git commit -m 1 &&
	git repack -adl &&
	echo "$(pwd)"/.git/objects/../objects >.git/objects/info/alternates
'

test_expect_success 're-packing repository with itsself as alternate' '
	git repack -adl &&
	git fsck
'

test_done
