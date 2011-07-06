#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
test_description='git svn commit-diff clobber'
. ./lib-git-svn.sh

test_expect_success 'initialize repo' '
	mkdir import &&
	cd import &&
	echo initial > file &&
	svn_cmd import -m "initial" . "$svnrepo" &&
	cd .. &&
	echo initial > file &&
	git update-index --add file &&
	git commit -a -m "initial"
	'
test_expect_success 'commit change from svn side' '
	svn_cmd co "$svnrepo" t.svn &&
	cd t.svn &&
	echo second line from svn >> file &&
	poke file &&
	svn_cmd commit -m "second line from svn" &&
	cd .. &&
	rm -rf t.svn
	'

test_expect_success 'commit conflicting change from git' '
	echo second line from git >> file &&
	git commit -a -m "second line from git" &&
	test_must_fail git svn commit-diff -r1 HEAD~1 HEAD "$svnrepo"
'

test_expect_success 'commit complementing change from git' '
	git reset --hard HEAD~1 &&
	echo second line from svn >> file &&
	git commit -a -m "second line from svn" &&
	echo third line from git >> file &&
	git commit -a -m "third line from git" &&
	git svn commit-diff -r2 HEAD~1 HEAD "$svnrepo"
	'

test_expect_success 'dcommit fails to commit because of conflict' '
	git svn init "$svnrepo" &&
	git svn fetch &&
	git reset --hard refs/${remotes_git_svn} &&
	svn_cmd co "$svnrepo" t.svn &&
	cd t.svn &&
	echo fourth line from svn >> file &&
	poke file &&
	svn_cmd commit -m "fourth line from svn" &&
	cd .. &&
	rm -rf t.svn &&
	echo "fourth line from git" >> file &&
	git commit -a -m "fourth line from git" &&
	test_must_fail git svn dcommit
	'

test_expect_success 'dcommit does the svn equivalent of an index merge' "
	git reset --hard refs/${remotes_git_svn} &&
	echo 'index merge' > file2 &&
	git update-index --add file2 &&
	git commit -a -m 'index merge' &&
	echo 'more changes' >> file2 &&
	git update-index file2 &&
	git commit -a -m 'more changes' &&
	git svn dcommit
	"

test_expect_success 'commit another change from svn side' '
	svn_cmd co "$svnrepo" t.svn &&
	cd t.svn &&
		echo third line from svn >> file &&
		poke file &&
		svn_cmd commit -m "third line from svn" &&
	cd .. &&
	rm -rf t.svn
	'

test_expect_success 'multiple dcommit from git svn will not clobber svn' "
	git reset --hard refs/${remotes_git_svn} &&
	echo new file >> new-file &&
	git update-index --add new-file &&
	git commit -a -m 'new file' &&
	echo clobber > file &&
	git commit -a -m 'clobber' &&
	test_must_fail git svn dcommit
	"


test_expect_success 'check that rebase really failed' '
	test -d .git/rebase-apply
'

test_expect_success 'resolve, continue the rebase and dcommit' "
	echo clobber and I really mean it > file &&
	git update-index file &&
	git rebase --continue &&
	git svn dcommit
	"

test_done
