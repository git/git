#!/bin/sh
#
# Copyright (c) 2010 Jay Soffian
#

test_description='but svn --rewrite-uuid test'

. ./lib-but-svn.sh

uuid=6cc8ada4-5932-4b4a-8242-3534ed8a3232

test_expect_success 'load svn repo' "
	svnadmin load -q '$rawsvnrepo' < '$TEST_DIRECTORY/t9153/svn.dump' &&
	but svn init --minimize-url --rewrite-uuid='$uuid' '$svnrepo' &&
	but svn fetch
	"

test_expect_success 'verify uuid' "
	but cat-file cummit refs/remotes/but-svn~0 >actual &&
	grep '^but-svn-id: .*@2 $uuid$' actual &&
	but cat-file cummit refs/remotes/but-svn~1 >actual &&
	grep '^but-svn-id: .*@1 $uuid$' actual
	"

test_done
