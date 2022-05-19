#!/bin/sh

test_description='but-status with core.ignorecase=true'

. ./test-lib.sh

test_expect_success 'status with hash collisions' '
	# note: "V/", "V/XQANY/" and "WURZAUP/" produce the same hash code
	# in name-hash.c::hash_name
	mkdir V &&
	mkdir V/XQANY &&
	mkdir WURZAUP &&
	touch V/XQANY/test &&
	but config core.ignorecase true &&
	but add . &&
	# test is successful if but status completes (no endless loop)
	but status
'

test_done
