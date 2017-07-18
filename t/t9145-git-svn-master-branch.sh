#!/bin/sh
#
# Copyright (c) 2009 Eric Wong
#
test_description='git svn initial master branch is "trunk" if possible'
. ./lib-git-svn.sh

test_expect_success 'setup test repository' '
	mkdir i &&
	> i/a &&
	svn_cmd import -m trunk i "$svnrepo/trunk" &&
	svn_cmd import -m b/a i "$svnrepo/branches/a" &&
	svn_cmd import -m b/b i "$svnrepo/branches/b"
'

test_expect_success 'git svn clone --stdlayout sets up trunk as master' '
	git svn clone -s "$svnrepo" g &&
	(
		cd g &&
		test x$(git rev-parse --verify refs/remotes/origin/trunk^0) = \
		     x$(git rev-parse --verify refs/heads/master^0)
	)
'

test_done
