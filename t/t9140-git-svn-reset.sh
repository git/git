#!/bin/sh
#
# Copyright (c) 2009 Ben Jackson
#

test_description='git svn reset'
. ./lib-git-svn.sh

test_expect_success 'setup test repository' '
	svn_cmd co "$svnrepo" s &&
	(
		cd s &&
		mkdir vis &&
		echo always visible > vis/vis.txt &&
		svn_cmd add vis &&
		svn_cmd commit -m "create visible files" &&
		mkdir hid &&
		echo initially hidden > hid/hid.txt &&
		svn_cmd add hid &&
		svn_cmd commit -m "create initially hidden files" &&
		svn_cmd up &&
		echo mod >> vis/vis.txt &&
		svn_cmd commit -m "modify vis" &&
		svn_cmd up
	)
'

test_expect_success 'clone SVN repository with hidden directory' '
	git svn init "$svnrepo" g &&
	( cd g && git svn fetch --ignore-paths="^hid" )
'

test_expect_success 'modify hidden file in SVN repo' '
	( cd s &&
	  echo mod hidden >> hid/hid.txt &&
	  svn_cmd commit -m "modify hid" &&
	  svn_cmd up
	)
'

test_expect_success 'fetch fails on modified hidden file' '
	( cd g &&
	  git svn find-rev refs/remotes/git-svn > ../expect &&
	  ! git svn fetch 2> ../errors &&
	  git svn find-rev refs/remotes/git-svn > ../expect2 ) &&
	fgrep "not found in commit" errors &&
	test_cmp expect expect2
'

test_expect_success 'reset unwinds back to r1' '
	( cd g &&
	  git svn reset -r1 &&
	  git svn find-rev refs/remotes/git-svn > ../expect2 ) &&
	echo 1 >expect &&
	test_cmp expect expect2
'

test_expect_success 'refetch succeeds not ignoring any files' '
	( cd g &&
	  git svn fetch &&
	  git svn rebase &&
	  fgrep "mod hidden" hid/hid.txt
	)
'

test_done
