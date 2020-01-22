#!/bin/sh

test_description='git svn dcommit CRLF'
. ./lib-git-svn.sh

test_expect_success 'setup commit repository' '
	svn_cmd mkdir -m "$test_description" "$svnrepo/dir" &&
	git svn clone "$svnrepo" work &&
	(
		cd work &&
		echo foo >>foo &&
		git update-index --add foo &&
		printf "a\\r\\n\\r\\nb\\r\\nc\\r\\n" >cmt &&
		p=$(git rev-parse HEAD) &&
		t=$(git write-tree) &&
		cmt=$(git commit-tree -p $p $t <cmt) &&
		git update-ref refs/heads/master $cmt &&
		git cat-file commit HEAD | tail -n4 >out &&
		test_cmp cmt out &&
		git svn dcommit &&
		printf "a\\n\\nb\\nc\\n" >exp &&
		git cat-file commit HEAD | sed -ne 6,9p >out &&
		test_cmp exp out
	)
'

test_done
