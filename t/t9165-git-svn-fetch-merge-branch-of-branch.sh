#!/bin/sh
#
# Copyright (c) 2012 Steven Walter
#

test_description='but svn merge detection'
. ./lib-but-svn.sh

svn_ver="$(svn --version --quiet)"
case $svn_ver in
0.* | 1.[0-4].*)
	skip_all="skipping but-svn test - SVN too old ($svn_ver)"
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
		svn_cmd cummit -m "initial cummit" &&
		svn_cmd cp -m branch "$svnrepo"/trunk "$svnrepo"/branches/branch1 &&
		svn_cmd switch "$svnrepo"/branches/branch1 &&
		touch bar &&
		svn_cmd add bar &&
		svn_cmd cummit -m branch1 &&
		svn_cmd cp -m branch "$svnrepo"/branches/branch1 "$svnrepo"/branches/branch2 &&
		svn_cmd switch "$svnrepo"/branches/branch2 &&
		touch baz &&
		svn_cmd add baz &&
		svn_cmd cummit -m branch2 &&
		svn_cmd switch "$svnrepo"/trunk &&
		touch bar2 &&
		svn_cmd add bar2 &&
		svn_cmd cummit -m trunk &&
		svn_cmd switch "$svnrepo"/branches/branch2 &&
		svn_cmd merge "$svnrepo"/trunk &&
		svn_cmd cummit -m "merge trunk" &&
		svn_cmd switch "$svnrepo"/trunk &&
		svn_cmd merge --reintegrate "$svnrepo"/branches/branch2 &&
		svn_cmd cummit -m "merge branch2"
	) &&
	rm -rf "$SVN_TREE"
'

test_expect_success 'clone svn repo' '
	but svn init -s "$svnrepo" &&
	but svn fetch
'

test_expect_success 'verify merge cummit' 'x=$(but rev-parse HEAD^2) &&
	y=$(but rev-parse origin/branch2) &&
	test "x$x" = "x$y"
'

test_done
