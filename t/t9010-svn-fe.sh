#!/bin/sh

test_description='check svn dumpfile importer'

. ./test-lib.sh

reinit_git () {
	rm -fr .git &&
	git init
}

>empty

test_expect_success 'empty dump' '
	reinit_git &&
	echo "SVN-fs-dump-format-version: 2" >input &&
	test-svn-fe input >stream &&
	git fast-import <stream
'

test_expect_success 'v3 dumps not supported' '
	reinit_git &&
	echo "SVN-fs-dump-format-version: 3" >input &&
	test_must_fail test-svn-fe input >stream &&
	test_cmp empty stream
'

test_expect_success 'set up svn repo' '
	svnconf=$PWD/svnconf &&
	mkdir -p "$svnconf" &&

	if
		svnadmin -h >/dev/null 2>&1 &&
		svnadmin create simple-svn &&
		svnadmin load simple-svn <"$TEST_DIRECTORY/t9135/svn.dump" &&
		svn export --config-dir "$svnconf" "file://$PWD/simple-svn" simple-svnco
	then
		test_set_prereq SVNREPO
	fi
'

test_expect_success SVNREPO 't9135/svn.dump' '
	git init simple-git &&
	test-svn-fe "$TEST_DIRECTORY/t9135/svn.dump" >simple.fe &&
	(
		cd simple-git &&
		git fast-import <../simple.fe
	) &&
	(
		cd simple-svnco &&
		git init &&
		git add . &&
		git fetch ../simple-git master &&
		git diff --exit-code FETCH_HEAD
	)
'

test_done
