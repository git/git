#!/bin/sh
#
# Copyright (c) 2010 Jay Soffian
#

test_description='but svn fancy glob test'

. ./lib-but-svn.sh

test_expect_success 'load svn repo' "
	svnadmin load -q '$rawsvnrepo' < '$TEST_DIRECTORY/t9154/svn.dump' &&
	but svn init --minimize-url -T trunk '$svnrepo' &&
	but svn fetch
	"

test_expect_success 'add red branch' "
	but config svn-remote.svn.branches 'branches/{red}:refs/remotes/*' &&
	but svn fetch &&
	but rev-parse refs/remotes/red &&
	test_must_fail but rev-parse refs/remotes/green &&
	test_must_fail but rev-parse refs/remotes/blue
	"

test_expect_success 'add gre branch' "
	but config --file=.but/svn/.metadata --unset svn-remote.svn.branches-maxRev &&
	but config svn-remote.svn.branches 'branches/{red,gre}:refs/remotes/*' &&
	but svn fetch &&
	but rev-parse refs/remotes/red &&
	test_must_fail but rev-parse refs/remotes/green &&
	test_must_fail but rev-parse refs/remotes/blue
	"

test_expect_success 'add green branch' "
	but config --file=.but/svn/.metadata --unset svn-remote.svn.branches-maxRev &&
	but config svn-remote.svn.branches 'branches/{red,green}:refs/remotes/*' &&
	but svn fetch &&
	but rev-parse refs/remotes/red &&
	but rev-parse refs/remotes/green &&
	test_must_fail but rev-parse refs/remotes/blue
	"

test_expect_success 'add all branches' "
	but config --file=.but/svn/.metadata --unset svn-remote.svn.branches-maxRev &&
	but config svn-remote.svn.branches 'branches/*:refs/remotes/*' &&
	but svn fetch &&
	but rev-parse refs/remotes/red &&
	but rev-parse refs/remotes/green &&
	but rev-parse refs/remotes/blue
	"

test_done
