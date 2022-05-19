#!/bin/sh
#
# Copyright (c) 2013 Tobias Schulte
#

test_description='but svn branch for subproject clones'

. ./lib-but-svn.sh

test_expect_success 'initialize svnrepo' '
	mkdir import &&
	(
		cd import &&
		mkdir -p trunk/project branches tags &&
		(
			cd trunk/project &&
			echo foo > foo
		) &&
		svn_cmd import -m "import for but-svn" . "$svnrepo" >/dev/null
	) &&
	rm -rf import &&
	svn_cmd co "$svnrepo"/trunk/project trunk/project &&
	(
		cd trunk/project &&
		echo bar >> foo &&
		svn_cmd ci -m "updated trunk"
	) &&
	rm -rf trunk
'

test_expect_success 'import into but' '
	but svn init --trunk=trunk/project --branches=branches/*/project \
		--tags=tags/*/project "$svnrepo" &&
	but svn fetch &&
	but checkout remotes/origin/trunk
'

test_expect_success 'but svn branch tests' '
	test_must_fail but svn branch a &&
	but svn branch --parents a &&
	test_must_fail but svn branch -t tag1 &&
	but svn branch --parents -t tag1 &&
	test_must_fail but svn branch --tag tag2 &&
	but svn branch --parents --tag tag2 &&
	test_must_fail but svn tag tag3 &&
	but svn tag --parents tag3
'

test_done
