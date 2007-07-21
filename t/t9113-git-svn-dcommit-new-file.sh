#!/bin/sh
#
# Copyright (c) 2007 Eric Wong
#

# Don't run this test by default unless the user really wants it
# I don't like the idea of taking a port and possibly leaving a
# daemon running on a users system if the test fails.
# Not all git users will need to interact with SVN.
test -z "$SVNSERVE_PORT" && exit 0

test_description='git-svn dcommit new files over svn:// test'

. ./lib-git-svn.sh

start_svnserve () {
	svnserve --listen-port $SVNSERVE_PORT \
	         --root $rawsvnrepo \
	         --listen-once \
	         --listen-host 127.0.0.1 &
}

test_expect_success 'start tracking an empty repo' "
	svn mkdir -m 'empty dir' $svnrepo/empty-dir &&
	echo anon-access = write >> $rawsvnrepo/conf/svnserve.conf &&
	start_svnserve &&
	git svn init svn://127.0.0.1:$SVNSERVE_PORT &&
	git svn fetch
	"

test_expect_success 'create files in new directory with dcommit' "
	mkdir git-new-dir &&
	echo hello > git-new-dir/world &&
	git update-index --add git-new-dir/world &&
	git commit -m hello &&
	start_svnserve &&
	git svn dcommit
	"

test_done
