#!/bin/sh
#
# Copyright (c) 2011 Frédéric Heitzmann

test_description='git svn dcommit --interactive series'

. ./lib-git-svn.sh

test_expect_success 'initialize repo' '
	svn_cmd mkdir -m"mkdir test-interactive" "$svnrepo/test-interactive" &&
	git svn clone "$svnrepo/test-interactive" test-interactive &&
	cd test-interactive &&
	touch foo && git add foo && git commit -m"foo: first commit" &&
	git svn dcommit
	'

test_expect_success 'answers: y [\n] yes' '
	(
		echo "change #1" >> foo && git commit -a -m"change #1" &&
		echo "change #2" >> foo && git commit -a -m"change #2" &&
		echo "change #3" >> foo && git commit -a -m"change #3" &&
		( echo "y

y" | GIT_SVN_NOTTY=1 git svn dcommit --interactive ) &&
		test $(git rev-parse HEAD) = $(git rev-parse remotes/git-svn)
	)
	'

test_expect_success 'answers: yes yes no' '
	(
		echo "change #1" >> foo && git commit -a -m"change #1" &&
		echo "change #2" >> foo && git commit -a -m"change #2" &&
		echo "change #3" >> foo && git commit -a -m"change #3" &&
		( echo "yes
yes
no" | GIT_SVN_NOTTY=1 git svn dcommit --interactive ) &&
		test $(git rev-parse HEAD^^^) = $(git rev-parse remotes/git-svn) &&
		git reset --hard remotes/git-svn
	)
	'

test_expect_success 'answers: yes quit' '
	(
		echo "change #1" >> foo && git commit -a -m"change #1" &&
		echo "change #2" >> foo && git commit -a -m"change #2" &&
		echo "change #3" >> foo && git commit -a -m"change #3" &&
		( echo "yes
quit" | GIT_SVN_NOTTY=1 git svn dcommit --interactive ) &&
		test $(git rev-parse HEAD^^^) = $(git rev-parse remotes/git-svn) &&
		git reset --hard remotes/git-svn
	)
	'

test_expect_success 'answers: all' '
	(
		echo "change #1" >> foo && git commit -a -m"change #1" &&
		echo "change #2" >> foo && git commit -a -m"change #2" &&
		echo "change #3" >> foo && git commit -a -m"change #3" &&
		( echo "all" | GIT_SVN_NOTTY=1 git svn dcommit --interactive ) &&
		test $(git rev-parse HEAD) = $(git rev-parse remotes/git-svn) &&
		git reset --hard remotes/git-svn
	)
	'

test_done
