#!/bin/sh
#
# Copyright (c) 2009 Eric Wong

test_description='but svn old rev_map preservd'
. ./lib-but-svn.sh

test_expect_success 'setup test repository with old layout' '
	mkdir i &&
	(cd i && > a) &&
	svn_cmd import -m- i "$svnrepo" &&
	but svn init "$svnrepo" &&
	but svn fetch &&
	test -d .but/svn/refs/remotes/but-svn/ &&
	! test -e .but/svn/but-svn/ &&
	mv .but/svn/refs/remotes/but-svn .but/svn/ &&
	rm -r .but/svn/refs
'

test_expect_success 'old layout continues to work' '
	svn_cmd import -m- i "$svnrepo/b" &&
	but svn rebase &&
	echo a >> b/a &&
	but add b/a &&
	but cummit -m- -a &&
	but svn dcummit &&
	! test -d .but/svn/refs/ &&
	test -e .but/svn/but-svn/
'

test_done
