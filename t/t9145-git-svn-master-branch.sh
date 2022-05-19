#!/bin/sh
#
# Copyright (c) 2009 Eric Wong
#
test_description='but svn initial main branch is "trunk" if possible'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./lib-but-svn.sh

test_expect_success 'setup test repository' '
	mkdir i &&
	> i/a &&
	svn_cmd import -m trunk i "$svnrepo/trunk" &&
	svn_cmd import -m b/a i "$svnrepo/branches/a" &&
	svn_cmd import -m b/b i "$svnrepo/branches/b"
'

test_expect_success 'but svn clone --stdlayout sets up trunk as main' '
	but svn clone -s "$svnrepo" g &&
	(
		cd g &&
		test x$(but rev-parse --verify refs/remotes/origin/trunk^0) = \
		     x$(but rev-parse --verify refs/heads/main^0)
	)
'

test_done
