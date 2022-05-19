#!/bin/sh
#
# Copyright (c) 2009 Eric Wong
#

test_description='but svn property tests'
. ./lib-but-svn.sh

test_expect_success 'setup repo with a but repo inside it' '
	svn_cmd co "$svnrepo" s &&
	(
		cd s &&
		but init &&
		test -f .but/HEAD &&
		> .but/a &&
		echo a > a &&
		svn_cmd add .but a &&
		svn_cmd cummit -m "create a nested but repo" &&
		svn_cmd up &&
		echo hi >> .but/a &&
		svn_cmd cummit -m "modify .but/a" &&
		svn_cmd up
	)
'

test_expect_success 'clone an SVN repo containing a but repo' '
	but svn clone "$svnrepo" g &&
	echo a > expect &&
	test_cmp expect g/a
'

test_expect_success 'SVN-side change outside of .but' '
	(
		cd s &&
		echo b >> a &&
		svn_cmd cummit -m "SVN-side change outside of .but" &&
		svn_cmd up &&
		svn_cmd log -v | fgrep "SVN-side change outside of .but"
	)
'

test_expect_success 'update but svn-cloned repo' '
	(
		cd g &&
		but svn rebase &&
		echo a > expect &&
		echo b >> expect &&
		test_cmp expect a &&
		rm expect
	)
'

test_expect_success 'SVN-side change inside of .but' '
	(
		cd s &&
		but add a &&
		but cummit -m "add a inside an SVN repo" &&
		but log &&
		svn_cmd add --force .but &&
		svn_cmd cummit -m "SVN-side change inside of .but" &&
		svn_cmd up &&
		svn_cmd log -v | fgrep "SVN-side change inside of .but"
	)
'

test_expect_success 'update but svn-cloned repo' '
	(
		cd g &&
		but svn rebase &&
		echo a > expect &&
		echo b >> expect &&
		test_cmp expect a &&
		rm expect
	)
'

test_expect_success 'SVN-side change in and out of .but' '
	(
		cd s &&
		echo c >> a &&
		but add a &&
		but cummit -m "add a inside an SVN repo" &&
		svn_cmd cummit -m "SVN-side change in and out of .but" &&
		svn_cmd up &&
		svn_cmd log -v | fgrep "SVN-side change in and out of .but"
	)
'

test_expect_success 'update but svn-cloned repo again' '
	(
		cd g &&
		but svn rebase &&
		echo a > expect &&
		echo b >> expect &&
		echo c >> expect &&
		test_cmp expect a &&
		rm expect
	)
'

test_done
