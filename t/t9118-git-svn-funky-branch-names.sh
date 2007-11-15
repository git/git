#!/bin/sh
#
# Copyright (c) 2007 Eric Wong
#

test_description='git-svn funky branch names'
. ./lib-git-svn.sh

test_expect_success 'setup svnrepo' "
	mkdir project project/trunk project/branches project/tags &&
	echo foo > project/trunk/foo &&
	svn import -m '$test_description' project \"$svnrepo/pr ject\" &&
	rm -rf project &&
	svn cp -m 'fun' \"$svnrepo/pr ject/trunk\" \
	                \"$svnrepo/pr ject/branches/fun plugin\" &&
	svn cp -m 'more fun!' \"$svnrepo/pr ject/branches/fun plugin\" \
	                      \"$svnrepo/pr ject/branches/more fun plugin!\" &&
	start_httpd
	"

test_expect_success 'test clone with funky branch names' "
	git svn clone -s \"$svnrepo/pr ject\" project &&
	cd project &&
		git rev-parse 'refs/remotes/fun%20plugin' &&
		git rev-parse 'refs/remotes/more%20fun%20plugin!' &&
	cd ..
	"

test_expect_success 'test dcommit to funky branch' "
	cd project &&
	git reset --hard 'refs/remotes/more%20fun%20plugin!' &&
	echo hello >> foo &&
	git commit -m 'hello' -- foo &&
	git svn dcommit &&
	cd ..
	"

stop_httpd

test_done
