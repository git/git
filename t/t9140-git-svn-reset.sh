#!/bin/sh
#
# Copyright (c) 2009 Ben Jackson
#

test_description='but svn reset'
. ./lib-but-svn.sh

test_expect_success 'setup test repository' '
	svn_cmd co "$svnrepo" s &&
	(
		cd s &&
		mkdir vis &&
		echo always visible > vis/vis.txt &&
		svn_cmd add vis &&
		svn_cmd cummit -m "create visible files" &&
		mkdir hid &&
		echo initially hidden > hid/hid.txt &&
		svn_cmd add hid &&
		svn_cmd cummit -m "create initially hidden files" &&
		svn_cmd up &&
		echo mod >> vis/vis.txt &&
		svn_cmd cummit -m "modify vis" &&
		svn_cmd up
	)
'

test_expect_success 'clone SVN repository with hidden directory' '
	but svn init "$svnrepo" g &&
	( cd g && but svn fetch --ignore-paths="^hid" )
'

test_expect_success 'modify hidden file in SVN repo' '
	( cd s &&
	  echo mod hidden >> hid/hid.txt &&
	  svn_cmd cummit -m "modify hid" &&
	  svn_cmd up
	)
'

test_expect_success 'fetch fails on modified hidden file' '
	( cd g &&
	  but svn find-rev refs/remotes/but-svn > ../expect &&
	  test_must_fail but svn fetch 2> ../errors &&
	  but svn find-rev refs/remotes/but-svn > ../expect2 ) &&
	fgrep "not found in cummit" errors &&
	test_cmp expect expect2
'

test_expect_success 'reset unwinds back to r1' '
	( cd g &&
	  but svn reset -r1 &&
	  but svn find-rev refs/remotes/but-svn > ../expect2 ) &&
	echo 1 >expect &&
	test_cmp expect expect2
'

test_expect_success 'refetch succeeds not ignoring any files' '
	( cd g &&
	  but svn fetch &&
	  but svn rebase &&
	  fgrep "mod hidden" hid/hid.txt
	)
'

test_done
