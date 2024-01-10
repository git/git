#!/bin/sh

test_description='git-status with core.ignorecase=true'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'status with hash collisions' '
	# note: "V/", "V/XQANY/" and "WURZAUP/" produce the same hash code
	# in name-hash.c::hash_name
	mkdir V &&
	mkdir V/XQANY &&
	mkdir WURZAUP &&
	touch V/XQANY/test &&
	git config core.ignorecase true &&
	git add . &&
	# test is successful if git status completes (no endless loop)
	git status
'

test_done
