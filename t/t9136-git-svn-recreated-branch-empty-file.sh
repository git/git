#!/bin/sh

test_description='test recreated svn branch with empty files'

. ./lib-git-svn.sh
test_expect_success 'load svn dumpfile'  '
	svnadmin load "$rawsvnrepo" < "${TEST_DIRECTORY}/t9136/svn.dump"
	'

test_expect_success 'clone using git svn' 'git svn clone -s "$svnrepo" x'

test_done
