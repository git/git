#!/bin/sh
#
# Copyright (c) 2012 Peter Baumann
#

test_description='git svn reset clears memoized caches'
. ./lib-git-svn.sh

svn_ver="$(svn --version --quiet)"
case $svn_ver in
0.* | 1.[0-4].*)
	skip_all="skipping git-svn test - SVN too old ($svn_ver)"
	test_done
	;;
esac

# ... a  -  b - m   <- trunk
#      \       /
#       ... c       <- branch1
#
# SVN Commits not interesting for this test are abbreviated with "..."
#
test_expect_success 'initialize source svn repo' '
	svn_cmd mkdir -m "create trunk" "$svnrepo"/trunk &&
	svn_cmd mkdir -m "create branches" "$svnrepo/branches" &&
	svn_cmd co "$svnrepo"/trunk "$SVN_TREE" &&
	(
		cd "$SVN_TREE" &&
		touch foo &&
		svn_cmd add foo &&
		svn_cmd commit -m "a" &&
		svn_cmd cp -m branch "$svnrepo"/trunk "$svnrepo"/branches/branch1 &&
		svn_cmd switch "$svnrepo"/branches/branch1 &&
		touch bar &&
		svn_cmd add bar &&
		svn_cmd commit -m b &&
		svn_cmd switch "$svnrepo"/trunk &&
		touch baz &&
		svn_cmd add baz &&
		svn_cmd commit -m c &&
		svn_cmd up &&
		svn_cmd merge "$svnrepo"/branches/branch1 &&
		svn_cmd commit -m "m"
	) &&
	rm -rf "$SVN_TREE"
'

test_expect_success 'fetch to merge-base (a)' '
	git svn init -s "$svnrepo" &&
	git svn fetch --revision BASE:3
'

# git svn rebase looses the merge commit
#
# ... a  -  b - m  <- trunk
#      \
#       ... c
#
test_expect_success 'rebase looses SVN merge (m)' '
	git svn rebase &&
	git svn fetch &&
	test 1 = $(git cat-file -p master|grep parent|wc -l)
'

# git svn fetch creates correct history with merge commit
#
# ... a  -  b - m  <- trunk
#      \       /
#       ... c      <- branch1
#
test_expect_success 'reset and fetch gets the SVN merge (m) correctly' '
	git svn reset -r 3 &&
	git reset --hard trunk &&
	git svn fetch &&
	test 2 = $(git cat-file -p trunk|grep parent|wc -l)
'

test_done
