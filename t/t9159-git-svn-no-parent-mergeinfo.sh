#!/bin/sh
test_description='git svn handling of root commits in merge ranges'
. ./lib-git-svn.sh

svn_ver="$(svn --version --quiet)"
case $svn_ver in
0.* | 1.[0-4].*)
	skip_all="skipping git-svn test - SVN too old ($svn_ver)"
	test_done
	;;
esac

test_expect_success 'test handling of root commits in merge ranges' '
	mkdir -p init/trunk init/branches init/tags &&
	echo "r1" > init/trunk/file.txt &&
	svn_cmd import -m "initial import" init "$svnrepo" &&
	svn_cmd co "$svnrepo" tmp &&
	(
		cd tmp &&
		echo "r2" > trunk/file.txt &&
		svn_cmd commit -m "Modify file.txt on trunk" &&
		svn_cmd cp trunk@1 branches/a &&
		svn_cmd commit -m "Create branch a from trunk r1" &&
		svn_cmd propset svn:mergeinfo /trunk:1-2 branches/a &&
		svn_cmd commit -m "Fake merge of trunk r2 into branch a" &&
		mkdir branches/b &&
		echo "r5" > branches/b/file2.txt &&
		svn_cmd add branches/b &&
		svn_cmd commit -m "Create branch b from thin air" &&
		echo "r6" > branches/b/file2.txt &&
		svn_cmd commit -m "Modify file2.txt on branch b" &&
		svn_cmd cp branches/b@5 branches/c &&
		svn_cmd commit -m "Create branch c from branch b r5" &&
		svn_cmd propset svn:mergeinfo /branches/b:5-6 branches/c &&
		svn_cmd commit -m "Fake merge of branch b r6 into branch c"
	) &&
	git svn init -s "$svnrepo" &&
	git svn fetch
	'

test_done
