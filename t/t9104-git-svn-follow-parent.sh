#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

test_description='git-svn --follow-parent fetching'
. ./lib-git-svn.sh

test_expect_success 'initialize repo' "
	mkdir import &&
	cd import &&
	mkdir -p trunk &&
	echo hello > trunk/readme &&
	svn import -m 'initial' . $svnrepo &&
	cd .. &&
	svn co $svnrepo wc &&
	cd wc &&
	echo world >> trunk/readme &&
	poke trunk/readme &&
	svn commit -m 'another commit' &&
	svn up &&
	svn mv -m 'rename to thunk' trunk thunk &&
	svn up &&
	echo goodbye >> thunk/readme &&
	poke thunk/readme &&
	svn commit -m 'bye now' &&
	cd ..
	"

test_expect_success 'init and fetch --follow-parent a moved directory' "
	git-svn init -i thunk $svnrepo/thunk &&
	git-svn fetch --follow-parent -i thunk &&
	git-rev-parse --verify refs/remotes/trunk &&
	test '$?' -eq '0'
	"

test_debug 'gitk --all &'

test_done
