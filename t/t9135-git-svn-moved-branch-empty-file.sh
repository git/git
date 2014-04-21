#!/bin/sh

test_description='test moved svn branch with missing empty files'

. ./lib-git-svn.sh
test_expect_success 'load svn dumpfile'  '
	svnadmin load "$rawsvnrepo" < "${TEST_DIRECTORY}/t9135/svn.dump"
	'

test_expect_success 'clone using git svn' 'git svn clone -s "$svnrepo" x'

test_expect_success 'test that b1 exists and is empty' '
	(
		cd x &&
		git reset --hard origin/branch-c &&
		test -f b1 &&
		! test -s b1
	)
	'

test_done
