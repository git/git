#!/bin/sh
#
# Copyright (c) 2012 Peter Baumann
#

test_description='but svn reset clears memoized caches'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./lib-but-svn.sh

svn_ver="$(svn --version --quiet)"
case $svn_ver in
0.* | 1.[0-4].*)
	skip_all="skipping but-svn test - SVN too old ($svn_ver)"
	test_done
	;;
esac

# ... a  -  b - m   <- trunk
#      \       /
#       ... c       <- branch1
#
# SVN cummits not interesting for this test are abbreviated with "..."
#
test_expect_success 'initialize source svn repo' '
	svn_cmd mkdir -m "create trunk" "$svnrepo"/trunk &&
	svn_cmd mkdir -m "create branches" "$svnrepo/branches" &&
	svn_cmd co "$svnrepo"/trunk "$SVN_TREE" &&
	(
		cd "$SVN_TREE" &&
		touch foo &&
		svn_cmd add foo &&
		svn_cmd cummit -m "a" &&
		svn_cmd cp -m branch "$svnrepo"/trunk "$svnrepo"/branches/branch1 &&
		svn_cmd switch "$svnrepo"/branches/branch1 &&
		touch bar &&
		svn_cmd add bar &&
		svn_cmd cummit -m b &&
		svn_cmd switch "$svnrepo"/trunk &&
		touch baz &&
		svn_cmd add baz &&
		svn_cmd cummit -m c &&
		svn_cmd up &&
		svn_cmd merge "$svnrepo"/branches/branch1 &&
		svn_cmd cummit -m "m"
	) &&
	rm -rf "$SVN_TREE"
'

test_expect_success 'fetch to merge-base (a)' '
	but svn init -s "$svnrepo" &&
	but svn fetch --revision BASE:3
'

# but svn rebase looses the merge cummit
#
# ... a  -  b - m  <- trunk
#      \
#       ... c
#
test_expect_success 'rebase looses SVN merge (m)' '
	but svn rebase &&
	but svn fetch &&
	test 1 = $(but cat-file -p main|grep parent|wc -l)
'

# but svn fetch creates correct history with merge cummit
#
# ... a  -  b - m  <- trunk
#      \       /
#       ... c      <- branch1
#
test_expect_success 'reset and fetch gets the SVN merge (m) correctly' '
	but svn reset -r 3 &&
	but reset --hard origin/trunk &&
	but svn fetch &&
	test 2 = $(but cat-file -p origin/trunk|grep parent|wc -l)
'

test_done
