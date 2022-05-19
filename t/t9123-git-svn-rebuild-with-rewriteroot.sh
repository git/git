#!/bin/sh
#
# Copyright (c) 2008 Jan Krüger
#

test_description='but svn respects rewriteRoot during rebuild'

. ./lib-but-svn.sh

mkdir import
(cd import
	touch foo
	svn_cmd import -m 'import for but svn' . "$svnrepo" >/dev/null
)
rm -rf import

test_expect_success 'init, fetch and checkout repository' '
	but svn init --rewrite-root=http://invalid.invalid/ "$svnrepo" &&
	but svn fetch &&
	but checkout -b mybranch remotes/but-svn
	'

test_expect_success 'remove rev_map' '
	rm "$GIT_SVN_DIR"/.rev_map.*
	'

test_expect_success 'rebuild rev_map' '
	but svn rebase >/dev/null
	'

test_done

