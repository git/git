#!/bin/sh
#
# Copyright (c) 2008 Jan Krüger
#

test_description='git svn respects rewriteRoot during rebuild'

. ./lib-git-svn.sh

mkdir import
(cd import
	touch foo
	svn_cmd import -m 'import for git svn' . "$svnrepo" >/dev/null
)
rm -rf import

test_expect_success 'init, fetch and checkout repository' '
	git svn init --rewrite-root=http://invalid.invalid/ "$svnrepo" &&
	git svn fetch &&
	git checkout -b mybranch ${remotes_git_svn}
	'

test_expect_success 'remove rev_map' '
	rm "$GIT_SVN_DIR"/.rev_map.*
	'

test_expect_success 'rebuild rev_map' '
	git svn rebase >/dev/null
	'

test_done

