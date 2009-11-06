#!/bin/sh
#
# Copyright (c) 2007, 2009 Sam Vilain
#

test_description='git-svn svn mergeinfo properties'

. ./lib-git-svn.sh

test_expect_success 'load svn dump' "
	svnadmin load -q '$rawsvnrepo' \
	  < '$TEST_DIRECTORY/t9151/svn-mergeinfo.dump' &&
	git svn init --minimize-url -R svnmerge \
	  -T trunk -b branches '$svnrepo' &&
	git svn fetch --all
	"

test_expect_success 'svn merges were represented coming in' "
	[ `git cat-file commit HEAD | grep parent | wc -l` -eq 2 ]
	"

test_done
