#!/bin/sh
#
# Copyright (c) 2008 Deskin Miller
#

test_description='but svn partial-rebuild tests'
. ./lib-but-svn.sh

test_expect_success 'initialize svnrepo' '
	mkdir import &&
	(
		(cd import &&
		mkdir trunk branches tags &&
		(cd trunk &&
		echo foo > foo
		) &&
		svn_cmd import -m "import for but-svn" . "$svnrepo" >/dev/null &&
		svn_cmd copy "$svnrepo"/trunk "$svnrepo"/branches/a \
			-m "created branch a"
		) &&
		rm -rf import &&
		svn_cmd co "$svnrepo"/trunk trunk &&
		(cd trunk &&
		echo bar >> foo &&
		svn_cmd ci -m "updated trunk"
		) &&
		svn_cmd co "$svnrepo"/branches/a a &&
		(cd a &&
		echo baz >> a &&
		svn_cmd add a &&
		svn_cmd ci -m "updated a"
		) &&
		but svn init --stdlayout "$svnrepo"
	)
'

test_expect_success 'import an early SVN revision into but' '
	but svn fetch -r1:2
'

test_expect_success 'make full but mirror of SVN' '
	mkdir mirror &&
	(
		(cd mirror &&
		but init &&
		but svn init --stdlayout "$svnrepo" &&
		but svn fetch
		)
	)
'

test_expect_success 'fetch from but mirror and partial-rebuild' '
	but config --add remote.origin.url "file://$PWD/mirror/.but" &&
	but config --add remote.origin.fetch refs/remotes/*:refs/remotes/* &&
	but fetch origin &&
	but svn fetch
'

test_done
