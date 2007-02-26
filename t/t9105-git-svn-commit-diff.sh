#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
test_description='git-svn commit-diff'
. ./lib-git-svn.sh

test_expect_success 'initialize repo' "
	mkdir import &&
	cd import &&
	echo hello > readme &&
	svn import -m 'initial' . $svnrepo &&
	cd .. &&
	echo hello > readme &&
	git update-index --add readme &&
	git commit -a -m 'initial' &&
	echo world >> readme &&
	git commit -a -m 'another'
	"

head=`git rev-parse --verify HEAD^0`
prev=`git rev-parse --verify HEAD^1`

# the internals of the commit-diff command are the same as the regular
# commit, so only a basic test of functionality is needed since we've
# already tested commit extensively elsewhere

test_expect_success 'test the commit-diff command' "
	test -n '$prev' && test -n '$head' &&
	git-svn commit-diff -r1 '$prev' '$head' '$svnrepo' &&
	svn co $svnrepo wc &&
	cmp readme wc/readme
	"

test_expect_success 'commit-diff to a sub-directory (with git-svn config)' "
	svn import -m 'sub-directory' import $svnrepo/subdir &&
	git-svn init $svnrepo/subdir &&
	git-svn fetch &&
	git-svn commit-diff -r3 '$prev' '$head' &&
	svn cat $svnrepo/subdir/readme > readme.2 &&
	cmp readme readme.2
	"

test_done
