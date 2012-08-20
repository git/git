#!/bin/sh
#
# Copyright (c) 2012 Steven Walter
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
		svn_cmd add foo &&
		svn_cmd commit -m "initial commit" &&
		svn_cmd cp -m branch "$svnrepo"/trunk "$svnrepo"/branches/branch1 &&
		svn_cmd switch "$svnrepo"/branches/branch1 &&
		touch bar &&
		svn_cmd add bar &&
		svn_cmd commit -m branch1 &&
		svn_cmd cp -m branch "$svnrepo"/branches/branch1 "$svnrepo"/branches/branch2 &&
		svn_cmd switch "$svnrepo"/branches/branch2 &&
		touch baz &&
		svn_cmd add baz &&
		svn_cmd commit -m branch2 &&
		svn_cmd switch "$svnrepo"/trunk &&
		touch bar2 &&
		svn_cmd add bar2 &&
		svn_cmd commit -m trunk &&
		svn_cmd switch "$svnrepo"/branches/branch2 &&
		svn_cmd merge "$svnrepo"/trunk &&
		svn_cmd commit -m "merge trunk"
		svn_cmd switch "$svnrepo"/trunk &&
		svn_cmd merge --reintegrate "$svnrepo"/branches/branch2 &&
		svn_cmd commit -m "merge branch2"
	) &&
	rm -rf "$SVN_TREE"
'

test_expect_success 'clone svn repo' '
	git svn init -s "$svnrepo" &&
	git svn fetch
'

test_expect_success 'verify merge commit' 'x=$(git rev-parse HEAD^2) &&
	y=$(git rev-parse branch2) &&
	test "x$x" = "x$y"
'

test_done
