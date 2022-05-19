#!/bin/sh
#
# Copyright (c) 2014 Ephrim Khong
#

test_description='repack involving cyclic alternate'
. ./test-lib.sh

test_expect_success setup '
	GIT_OBJECT_DIRECTORY=.but//../.but/objects &&
	export GIT_OBJECT_DIRECTORY &&
	touch a &&
	but add a &&
	but cummit -m 1 &&
	but repack -adl &&
	echo "$(pwd)"/.but/objects/../objects >.but/objects/info/alternates
'

test_expect_success 're-packing repository with itsself as alternate' '
	but repack -adl &&
	but fsck
'

test_done
