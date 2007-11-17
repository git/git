#!/bin/sh
#
# Copyright (c) 2007 Eric Wong
#

test_description='git-svn init/clone tests'

. ./lib-git-svn.sh

# setup, run inside tmp so we don't have any conflicts with $svnrepo
set -e
rm -r .git
mkdir tmp
cd tmp

test_expect_success 'setup svnrepo' "
	mkdir project project/trunk project/branches project/tags &&
	echo foo > project/trunk/foo &&
	svn import -m '$test_description' project $svnrepo/project &&
	rm -rf project
	"

test_expect_success 'basic clone' "
	test ! -d trunk &&
	git svn clone $svnrepo/project/trunk &&
	test -d trunk/.git/svn &&
	test -e trunk/foo &&
	rm -rf trunk
	"

test_expect_success 'clone to target directory' "
	test ! -d target &&
	git svn clone $svnrepo/project/trunk target &&
	test -d target/.git/svn &&
	test -e target/foo &&
	rm -rf target
	"

test_expect_success 'clone with --stdlayout' "
	test ! -d project &&
	git svn clone -s $svnrepo/project &&
	test -d project/.git/svn &&
	test -e project/foo &&
	rm -rf project
	"

test_expect_success 'clone to target directory with --stdlayout' "
	test ! -d target &&
	git svn clone -s $svnrepo/project target &&
	test -d target/.git/svn &&
	test -e target/foo &&
	rm -rf target
	"

test_done
