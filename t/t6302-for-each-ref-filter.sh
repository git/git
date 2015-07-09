#!/bin/sh

test_description='test for-each-refs usage of ref-filter APIs'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-gpg.sh

if ! test_have_prereq GPG
then
	skip_all="skipping for-each-ref tests, GPG not available"
	test_done
fi

test_expect_success 'setup some history and refs' '
	test_commit one &&
	test_commit two &&
	test_commit three &&
	git checkout -b side &&
	test_commit four &&
	git tag -s -m "A signed tag message" signed-tag &&
	git tag -s -m "Annonated doubly" double-tag signed-tag &&
	git checkout master &&
	git update-ref refs/odd/spot master
'

test_done
