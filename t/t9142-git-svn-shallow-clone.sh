#!/bin/sh
#
# Copyright (c) 2009 Eric Wong
#

test_description='git svn shallow clone'
. ./lib-git-svn.sh

test_expect_success 'setup test repository' '
	svn_cmd mkdir -m "create standard layout" \
	  "$svnrepo"/trunk "$svnrepo"/branches "$svnrepo"/tags &&
	svn_cmd cp -m "branch off trunk" \
	  "$svnrepo"/trunk "$svnrepo"/branches/a &&
	svn_cmd co "$svnrepo"/branches/a &&
	(
		cd a &&
		> foo &&
		svn_cmd add foo &&
		svn_cmd commit -m "add foo"
	)
'

start_httpd

test_expect_success 'clone trunk with "-r HEAD"' '
	git svn clone -r HEAD "$svnrepo/trunk" g &&
	( cd g && git rev-parse --symbolic --verify HEAD )
'

stop_httpd

test_done
