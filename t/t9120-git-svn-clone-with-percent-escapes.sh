#!/bin/sh
#
# Copyright (c) 2008 Kevin Ballard
#

test_description='git-svn clone with percent escapes'
. ./lib-git-svn.sh

test_expect_success 'setup svnrepo' "
	mkdir project project/trunk project/branches project/tags &&
	echo foo > project/trunk/foo &&
	svn import -m '$test_description' project '$svnrepo/pr ject' &&
	rm -rf project &&
	start_httpd
"

if test "$SVN_HTTPD_PORT" = ""
then
	test_expect_failure 'test clone with percent escapes - needs SVN_HTTPD_PORT set' 'false'
else
	test_expect_success 'test clone with percent escapes' '
		git svn clone "$svnrepo/pr%20ject" clone &&
		cd clone &&
			git rev-parse refs/remotes/git-svn &&
		cd ..
	'
fi

stop_httpd

test_done
