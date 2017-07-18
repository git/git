#!/bin/sh
#
# Copyright (c) 2010 Jay Soffian
#

test_description='git svn fancy glob test'

. ./lib-git-svn.sh

test_expect_success 'load svn repo' "
	svnadmin load -q '$rawsvnrepo' < '$TEST_DIRECTORY/t9154/svn.dump' &&
	git svn init --minimize-url -T trunk '$svnrepo' &&
	git svn fetch
	"

test_expect_success 'add red branch' "
	git config svn-remote.svn.branches 'branches/{red}:refs/remotes/*' &&
	git svn fetch &&
	git rev-parse refs/remotes/red &&
	test_must_fail git rev-parse refs/remotes/green &&
	test_must_fail git rev-parse refs/remotes/blue
	"

test_expect_success 'add gre branch' "
	git config --file=.git/svn/.metadata --unset svn-remote.svn.branches-maxRev &&
	git config svn-remote.svn.branches 'branches/{red,gre}:refs/remotes/*' &&
	git svn fetch &&
	git rev-parse refs/remotes/red &&
	test_must_fail git rev-parse refs/remotes/green &&
	test_must_fail git rev-parse refs/remotes/blue
	"

test_expect_success 'add green branch' "
	git config --file=.git/svn/.metadata --unset svn-remote.svn.branches-maxRev &&
	git config svn-remote.svn.branches 'branches/{red,green}:refs/remotes/*' &&
	git svn fetch &&
	git rev-parse refs/remotes/red &&
	git rev-parse refs/remotes/green &&
	test_must_fail git rev-parse refs/remotes/blue
	"

test_expect_success 'add all branches' "
	git config --file=.git/svn/.metadata --unset svn-remote.svn.branches-maxRev &&
	git config svn-remote.svn.branches 'branches/*:refs/remotes/*' &&
	git svn fetch &&
	git rev-parse refs/remotes/red &&
	git rev-parse refs/remotes/green &&
	git rev-parse refs/remotes/blue
	"

test_done
