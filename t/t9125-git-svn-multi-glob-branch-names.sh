#!/bin/sh
# Copyright (c) 2008 Marcus Griep

test_description='git svn multi-glob branch names'
. ./lib-git-svn.sh

test_expect_success 'setup svnrepo' '
	mkdir project project/trunk project/branches \
			project/branches/v14.1 project/tags &&
	echo foo > project/trunk/foo &&
	svn_cmd import -m "$test_description" project "$svnrepo/project" &&
	rm -rf project &&
	svn_cmd cp -m "fun" "$svnrepo/project/trunk" \
	                "$svnrepo/project/branches/v14.1/beta" &&
	svn_cmd cp -m "more fun!" "$svnrepo/project/branches/v14.1/beta" \
	                      "$svnrepo/project/branches/v14.1/gold"
	'

test_expect_success 'test clone with multi-glob in branch names' '
	git svn clone -T trunk -b branches/*/* -t tags \
	              "$svnrepo/project" project &&
	cd project &&
		git rev-parse "refs/remotes/v14.1/beta" &&
		git rev-parse "refs/remotes/v14.1/gold" &&
	cd ..
	'

test_expect_success 'test dcommit to multi-globbed branch' "
	cd project &&
	git reset --hard 'refs/remotes/v14.1/gold' &&
	echo hello >> foo &&
	git commit -m 'hello' -- foo &&
	git svn dcommit &&
	cd ..
	"

test_done
