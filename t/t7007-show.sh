#!/bin/sh

test_description='git show'

. ./test-lib.sh

test_expect_success setup '
	echo hello world >foo &&
	H=$(git hash-object -w foo) &&
	git tag -a foo-tag -m "Tags $H" $H &&
	HH=$(expr "$H" : "\(..\)") &&
	H38=$(expr "$H" : "..\(.*\)") &&
	rm -f .git/objects/$HH/$H38
'

test_expect_success 'showing a tag that point at a missing object' '
	test_must_fail git --no-pager show foo-tag
'

test_done
