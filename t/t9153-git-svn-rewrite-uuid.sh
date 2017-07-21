#!/bin/sh
#
# Copyright (c) 2010 Jay Soffian
#

test_description='git svn --rewrite-uuid test'

. ./lib-git-svn.sh

uuid=6cc8ada4-5932-4b4a-8242-3534ed8a3232

test_expect_success 'load svn repo' "
	svnadmin load -q '$rawsvnrepo' < '$TEST_DIRECTORY/t9153/svn.dump' &&
	git svn init --minimize-url --rewrite-uuid='$uuid' '$svnrepo' &&
	git svn fetch
	"

test_expect_success 'verify uuid' "
	git cat-file commit refs/remotes/git-svn~0 | \
	   grep '^git-svn-id: .*@2 $uuid$' &&
	git cat-file commit refs/remotes/git-svn~1 | \
	   grep '^git-svn-id: .*@1 $uuid$'
	"

test_done
