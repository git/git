#!/bin/sh
#
# Copyright (c) 2008 Deskin Miller
#

test_description='git svn partial-rebuild tests'
. ./lib-git-svn.sh

test_expect_success 'initialize svnrepo' '
	mkdir import &&
	(
		cd import &&
		mkdir trunk branches tags &&
		cd trunk &&
		echo foo > foo &&
		cd .. &&
		svn import -m "import for git-svn" . "$svnrepo" >/dev/null &&
		svn copy "$svnrepo"/trunk "$svnrepo"/branches/a \
			-m "created branch a" &&
		cd .. &&
		rm -rf import &&
		svn co "$svnrepo"/trunk trunk &&
		cd trunk &&
		echo bar >> foo &&
		svn ci -m "updated trunk" &&
		cd .. &&
		svn co "$svnrepo"/branches/a a &&
		cd a &&
		echo baz >> a &&
		svn add a &&
		svn ci -m "updated a" &&
		cd .. &&
		git svn init --stdlayout "$svnrepo"
	)
'

test_expect_success 'import an early SVN revision into git' '
	git svn fetch -r1:2
'

test_expect_success 'make full git mirror of SVN' '
	mkdir mirror &&
	(
		cd mirror &&
		git init &&
		git svn init --stdlayout "$svnrepo" &&
		git svn fetch &&
		cd ..
	)
'

test_expect_success 'fetch from git mirror and partial-rebuild' '
	git config --add remote.origin.url "file://$PWD/mirror/.git" &&
	git config --add remote.origin.fetch refs/remotes/*:refs/remotes/* &&
	git fetch origin &&
	git svn fetch
'

test_done
