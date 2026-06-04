#!/bin/sh
#
# Copyright (c) 2010 Steven Walter
#

test_description='git svn merge detection'
. ./lib-git-svn.sh

svn_ver="$(svn --version --quiet)"
case $svn_ver in
0.* | 1.[0-4].*)
	skip_all="skipping git-svn test - SVN too old ($svn_ver)"
	test_done
	;;
esac

test_expect_success 'initialize source svn repo' '
	svn_cmd mkdir -m x "$svnrepo"/trunk &&
	svn_cmd mkdir -m x "$svnrepo"/branches &&
	svn_cmd co "$svnrepo"/trunk "$SVN_TREE" &&
	(
		cd "$SVN_TREE" &&
		touch foo &&
		svn add foo &&
		svn commit -m "initial commit" &&
		svn cp -m branch "$svnrepo"/trunk "$svnrepo"/branches/branch1 &&
		touch bar &&
		svn add bar &&
		svn commit -m x &&
		svn cp -m branch "$svnrepo"/trunk "$svnrepo"/branches/branch2 &&
		svn switch "$svnrepo"/branches/branch1 &&
		touch baz &&
		svn add baz &&
		svn commit -m x &&
		svn switch "$svnrepo"/trunk &&
		svn merge "$svnrepo"/branches/branch1 &&
		svn commit -m "merge" &&
		svn switch "$svnrepo"/branches/branch1 &&
		svn commit -m x &&
		svn switch "$svnrepo"/branches/branch2 &&
		svn merge "$svnrepo"/branches/branch1 &&
		svn commit -m "merge branch1" &&
		svn switch "$svnrepo"/trunk &&
		svn merge "$svnrepo"/branches/branch2 &&
		svn resolved baz &&
		svn commit -m "merge branch2"
	) &&
	rm -rf "$SVN_TREE"
'

test_expect_success 'clone svn repo' '
	git svn init -s "$svnrepo" &&
	git svn fetch
'

test_expect_success 'verify merge commit' 'git rev-parse HEAD^2'

test_done
