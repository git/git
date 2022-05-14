#!/bin/sh
#
# Copyright (c) 2013 Tobias Schulte
#

test_description='git svn branch for subproject clones'

. ./lib-git-svn.sh

test_expect_success 'initialize svnrepo' '
	mkdir import &&
	(
		cd import &&
		mkdir -p trunk/project branches tags &&
		(
			cd trunk/project &&
			echo foo > foo
		) &&
		svn_cmd import -m "import for git-svn" . "$svnrepo" >/dev/null
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

test_expect_success 'import into git' '
	git svn init --trunk=trunk/project --branches=branches/*/project \
		--tags=tags/*/project "$svnrepo" &&
	git svn fetch &&
	git checkout remotes/origin/trunk
'

test_expect_success 'git svn branch tests' '
	test_must_fail git svn branch a &&
	git svn branch --parents a &&
	test_must_fail git svn branch -t tag1 &&
	git svn branch --parents -t tag1 &&
	test_must_fail git svn branch --tag tag2 &&
	git svn branch --parents --tag tag2 &&
	test_must_fail git svn tag tag3 &&
	git svn tag --parents tag3
'

test_done
