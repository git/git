#!/bin/sh
#
# Portions copyright (c) 2007, 2009 Sam Vilain
# Portions copyright (c) 2011 Bryan Jacobs
#

test_description='git-svn svn mergeinfo propagation'

. ./lib-git-svn.sh

test_expect_success 'load svn dump' "
	svnadmin load -q '$rawsvnrepo' \
	  < '$TEST_DIRECTORY/t9161/branches.dump' &&
	git svn init --minimize-url -R svnmerge \
	  -T trunk -b branches '$svnrepo' &&
	git svn fetch --all
	"

test_expect_success 'propagate merge information' '
	git config svn.pushmergeinfo yes &&
	git checkout origin/svnb1 &&
	git merge --no-ff origin/svnb2 &&
	git svn dcommit
	'

test_expect_success 'check svn:mergeinfo' '
	mergeinfo=$(svn_cmd propget svn:mergeinfo "$svnrepo"/branches/svnb1)
	test "$mergeinfo" = "/branches/svnb2:3,8"
	'

test_expect_success 'merge another branch' '
	git merge --no-ff origin/svnb3 &&
	git svn dcommit
	'

test_expect_success 'check primary parent mergeinfo respected' '
	mergeinfo=$(svn_cmd propget svn:mergeinfo "$svnrepo"/branches/svnb1)
	test "$mergeinfo" = "/branches/svnb2:3,8
/branches/svnb3:4,9"
	'

test_expect_success 'merge existing merge' '
	git merge --no-ff origin/svnb4 &&
	git svn dcommit
	'

test_expect_success "check both parents' mergeinfo respected" '
	mergeinfo=$(svn_cmd propget svn:mergeinfo "$svnrepo"/branches/svnb1)
	test "$mergeinfo" = "/branches/svnb2:3,8
/branches/svnb3:4,9
/branches/svnb4:5-6,10-12
/branches/svnb5:6,11"
	'

test_expect_success 'make further commits to branch' '
	git checkout origin/svnb2 &&
	touch newb2file &&
	git add newb2file &&
	git commit -m "later b2 commit" &&
	touch newb2file-2 &&
	git add newb2file-2 &&
	git commit -m "later b2 commit 2" &&
	git svn dcommit
	'

test_expect_success 'second forward merge' '
	git checkout origin/svnb1 &&
	git merge --no-ff origin/svnb2 &&
	git svn dcommit
	'

test_expect_success 'check new mergeinfo added' '
	mergeinfo=$(svn_cmd propget svn:mergeinfo "$svnrepo"/branches/svnb1)
	test "$mergeinfo" = "/branches/svnb2:3,8,16-17
/branches/svnb3:4,9
/branches/svnb4:5-6,10-12
/branches/svnb5:6,11"
	'

test_expect_success 'reintegration merge' '
	git checkout origin/svnb4 &&
	git merge --no-ff origin/svnb1 &&
	git svn dcommit
	'

test_expect_success 'check reintegration mergeinfo' '
	mergeinfo=$(svn_cmd propget svn:mergeinfo "$svnrepo"/branches/svnb4)
	test "$mergeinfo" = "/branches/svnb1:2-4,7-9,13-18
/branches/svnb2:3,8,16-17
/branches/svnb3:4,9
/branches/svnb5:6,11"
	'

test_expect_success 'dcommit a merge at the top of a stack' '
	git checkout origin/svnb1 &&
	touch anotherfile &&
	git add anotherfile &&
	git commit -m "a commit" &&
	git merge origin/svnb4 &&
	git svn dcommit
	'

test_done
