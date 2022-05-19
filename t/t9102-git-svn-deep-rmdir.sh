#!/bin/sh
test_description='but svn rmdir'

. ./lib-but-svn.sh

test_expect_success 'initialize repo' '
	mkdir import &&
	(
		cd import &&
		mkdir -p deeply/nested/directory/number/1 &&
		mkdir -p deeply/nested/directory/number/2 &&
		echo foo >deeply/nested/directory/number/1/file &&
		echo foo >deeply/nested/directory/number/2/another &&
		svn_cmd import -m "import for but svn" . "$svnrepo"
	)
	'

test_expect_success 'mirror via but svn' '
	but svn init "$svnrepo" &&
	but svn fetch &&
	but checkout -f -b test-rmdir remotes/but-svn
	'

test_expect_success 'Try a cummit on rmdir' '
	but rm -f deeply/nested/directory/number/2/another &&
	but cummit -a -m "remove another" &&
	but svn set-tree --rmdir HEAD &&
	svn_cmd ls -R "$svnrepo" | grep ^deeply/nested/directory/number/1
	'


test_done
