#!/bin/sh
#
# Copyright (c) 2007 Eric Wong
#

test_description='git-svn useSvnsyncProps test'

. ./lib-git-svn.sh

test_expect_success 'load svnsync repo' "
	svnadmin load -q $rawsvnrepo < ../t9111/svnsync.dump &&
	git-svn init -R arr -i bar $svnrepo/bar &&
	git-svn init -R argh -i dir $svnrepo/dir &&
	git-svn init -R argh -i e $svnrepo/dir/a/b/c/d/e &&
	git-config svn.useSvnsyncProps true &&
	git-svn fetch --all
	"

uuid=161ce429-a9dd-4828-af4a-52023f968c89

bar_url=http://mayonaise/svnrepo/bar
test_expect_success 'verify metadata for /bar' "
	git-cat-file commit refs/remotes/bar | \
	   grep '^git-svn-id: $bar_url@12 $uuid$' &&
	git-cat-file commit refs/remotes/bar~1 | \
	   grep '^git-svn-id: $bar_url@11 $uuid$' &&
	git-cat-file commit refs/remotes/bar~2 | \
	   grep '^git-svn-id: $bar_url@10 $uuid$' &&
	git-cat-file commit refs/remotes/bar~3 | \
	   grep '^git-svn-id: $bar_url@9 $uuid$' &&
	git-cat-file commit refs/remotes/bar~4 | \
	   grep '^git-svn-id: $bar_url@6 $uuid$' &&
	git-cat-file commit refs/remotes/bar~5 | \
	   grep '^git-svn-id: $bar_url@1 $uuid$'
	"

e_url=http://mayonaise/svnrepo/dir/a/b/c/d/e
test_expect_success 'verify metadata for /dir/a/b/c/d/e' "
	git-cat-file commit refs/remotes/e | \
	   grep '^git-svn-id: $e_url@1 $uuid$'
	"

dir_url=http://mayonaise/svnrepo/dir
test_expect_success 'verify metadata for /dir' "
	git-cat-file commit refs/remotes/dir | \
	   grep '^git-svn-id: $dir_url@2 $uuid$' &&
	git-cat-file commit refs/remotes/dir~1 | \
	   grep '^git-svn-id: $dir_url@1 $uuid$'
	"

test_done
