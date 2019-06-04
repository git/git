#!/bin/sh
#
# Copyright (c) 2009 Eric Wong
#

test_description='git svn property tests'
. ./lib-git-svn.sh

test_expect_success 'setup repo with a git repo inside it' '
	svn_cmd co "$svnrepo" s &&
	(
		cd s &&
		git init &&
		test -f .git/HEAD &&
		> .git/a &&
		echo a > a &&
		svn_cmd add .git a &&
		svn_cmd commit -m "create a nested git repo" &&
		svn_cmd up &&
		echo hi >> .git/a &&
		svn_cmd commit -m "modify .git/a" &&
		svn_cmd up
	)
'

test_expect_success 'clone an SVN repo containing a git repo' '
	git svn clone "$svnrepo" g &&
	echo a > expect &&
	test_cmp expect g/a
'

test_expect_success 'SVN-side change outside of .git' '
	(
		cd s &&
		echo b >> a &&
		svn_cmd commit -m "SVN-side change outside of .git" &&
		svn_cmd up &&
		svn_cmd log -v | fgrep "SVN-side change outside of .git"
	)
'

test_expect_success 'update git svn-cloned repo' '
	(
		cd g &&
		git svn rebase &&
		echo a > expect &&
		echo b >> expect &&
		test_cmp expect a &&
		rm expect
	)
'

test_expect_success 'SVN-side change inside of .git' '
	(
		cd s &&
		git add a &&
		git commit -m "add a inside an SVN repo" &&
		git log &&
		svn_cmd add --force .git &&
		svn_cmd commit -m "SVN-side change inside of .git" &&
		svn_cmd up &&
		svn_cmd log -v | fgrep "SVN-side change inside of .git"
	)
'

test_expect_success 'update git svn-cloned repo' '
	(
		cd g &&
		git svn rebase &&
		echo a > expect &&
		echo b >> expect &&
		test_cmp expect a &&
		rm expect
	)
'

test_expect_success 'SVN-side change in and out of .git' '
	(
		cd s &&
		echo c >> a &&
		git add a &&
		git commit -m "add a inside an SVN repo" &&
		svn_cmd commit -m "SVN-side change in and out of .git" &&
		svn_cmd up &&
		svn_cmd log -v | fgrep "SVN-side change in and out of .git"
	)
'

test_expect_success 'update git svn-cloned repo again' '
	(
		cd g &&
		git svn rebase &&
		echo a > expect &&
		echo b >> expect &&
		echo c >> expect &&
		test_cmp expect a &&
		rm expect
	)
'

test_done
