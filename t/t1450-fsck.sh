#!/bin/sh

test_description='git fsck random collection of tests'

. ./test-lib.sh

test_expect_success setup '
	test_commit A fileA one &&
	git checkout HEAD^0 &&
	test_commit B fileB two &&
	git tag -d A B &&
	git reflog expire --expire=now --all
'

test_expect_success 'HEAD is part of refs' '
	test 0 = $(git fsck | wc -l)
'

test_expect_success 'loose objects borrowed from alternate are not missing' '
	mkdir another &&
	(
		cd another &&
		git init &&
		echo ../../../.git/objects >.git/objects/info/alternates &&
		test_commit C fileC one &&
		git fsck >out &&
		! grep "missing blob" out
	)
'

test_done
