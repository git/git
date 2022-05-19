#!/bin/sh
# Copyright (c) 2008 Marcus Griep

test_description='but svn multi-glob branch names'
. ./lib-but-svn.sh

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
	but svn clone -T trunk -b branches/*/* -t tags \
	              "$svnrepo/project" project &&
	(cd project &&
		but rev-parse "refs/remotes/origin/v14.1/beta" &&
		but rev-parse "refs/remotes/origin/v14.1/gold"
	)
	'

test_expect_success 'test dcummit to multi-globbed branch' "
	(cd project &&
	but reset --hard 'refs/remotes/origin/v14.1/gold' &&
	echo hello >> foo &&
	but cummit -m 'hello' -- foo &&
	but svn dcummit
	)
	"

test_done
