#!/bin/sh
#
# Copyright (c) 2010 Steven Walter
#

test_description='but svn mergeinfo propagation'

. ./lib-but-svn.sh

test_expect_success 'initialize source svn repo' '
	svn_cmd mkdir -m x "$svnrepo"/trunk &&
	svn_cmd co "$svnrepo"/trunk "$SVN_TREE" &&
	(
		cd "$SVN_TREE" &&
		touch foo &&
		svn_cmd add foo &&
		svn_cmd cummit -m "initial cummit"
	) &&
	rm -rf "$SVN_TREE"
'

test_expect_success 'clone svn repo' '
	but svn init "$svnrepo"/trunk &&
	but svn fetch
'

test_expect_success 'change svn:mergeinfo' '
	touch bar &&
	but add bar &&
	but cummit -m "bar" &&
	but svn dcummit --mergeinfo="/branches/foo:1-10"
'

test_expect_success 'verify svn:mergeinfo' '
	mergeinfo=$(svn_cmd propget svn:mergeinfo "$svnrepo"/trunk) &&
	test "$mergeinfo" = "/branches/foo:1-10"
'

test_expect_success 'change svn:mergeinfo multiline' '
	touch baz &&
	but add baz &&
	but cummit -m "baz" &&
	but svn dcummit --mergeinfo="/branches/bar:1-10 /branches/other:3-5,8,10-11"
'

test_expect_success 'verify svn:mergeinfo multiline' '
	mergeinfo=$(svn_cmd propget svn:mergeinfo "$svnrepo"/trunk) &&
	test "$mergeinfo" = "/branches/bar:1-10
/branches/other:3-5,8,10-11"
'

test_done
