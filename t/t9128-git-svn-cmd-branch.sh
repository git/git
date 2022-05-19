#!/bin/sh
#
# Copyright (c) 2008 Deskin Miller
#

test_description='but svn partial-rebuild tests'

. ./lib-but-svn.sh

test_expect_success 'initialize svnrepo' '
	mkdir import &&
	(
		(cd import &&
		mkdir trunk branches tags &&
		(cd trunk &&
		echo foo > foo
		) &&
		svn_cmd import -m "import for but-svn" . "$svnrepo" >/dev/null
		) &&
		rm -rf import &&
		svn_cmd co "$svnrepo"/trunk trunk &&
		(cd trunk &&
		echo bar >> foo &&
		svn_cmd ci -m "updated trunk"
		) &&
		rm -rf trunk
	)
'

test_expect_success 'import into but' '
	but svn init --stdlayout "$svnrepo" &&
	but svn fetch &&
	but checkout remotes/origin/trunk
'

test_expect_success 'but svn branch tests' '
	but svn branch a &&
	base=$(but rev-parse HEAD:) &&
	test $base = $(but rev-parse remotes/origin/a:) &&
	but svn branch -m "created branch b blah" b &&
	test $base = $(but rev-parse remotes/origin/b:) &&
	test_must_fail but branch -m "no branchname" &&
	but svn branch -n c &&
	test_must_fail but rev-parse remotes/origin/c &&
	test_must_fail but svn branch a &&
	but svn branch -t tag1 &&
	test $base = $(but rev-parse remotes/origin/tags/tag1:) &&
	but svn branch --tag tag2 &&
	test $base = $(but rev-parse remotes/origin/tags/tag2:) &&
	but svn tag tag3 &&
	test $base = $(but rev-parse remotes/origin/tags/tag3:) &&
	but svn tag -m "created tag4 foo" tag4 &&
	test $base = $(but rev-parse remotes/origin/tags/tag4:) &&
	test_must_fail but svn tag -m "no tagname" &&
	but svn tag -n tag5 &&
	test_must_fail but rev-parse remotes/origin/tags/tag5 &&
	test_must_fail but svn tag tag1
'

test_expect_success 'branch uses correct svn-remote' '
	(svn_cmd co "$svnrepo" svn &&
	cd svn &&
	mkdir mirror &&
	svn_cmd add mirror &&
	svn_cmd copy trunk mirror/ &&
	svn_cmd copy tags mirror/ &&
	svn_cmd copy branches mirror/ &&
	svn_cmd ci -m "made mirror" ) &&
	rm -rf svn &&
	but svn init -s -R mirror --prefix=mirror/ "$svnrepo"/mirror &&
	but svn fetch -R mirror &&
	but checkout mirror/trunk &&
	base=$(but rev-parse HEAD:) &&
	but svn branch -m "branch in mirror" d &&
	test $base = $(but rev-parse remotes/mirror/d:) &&
	test_must_fail but rev-parse remotes/d
'

test_done
