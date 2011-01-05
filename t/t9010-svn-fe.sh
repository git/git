#!/bin/sh

test_description='check svn dumpfile importer'

. ./test-lib.sh

if ! svnadmin -h >/dev/null 2>&1
then
	skip_all='skipping svn-fe tests, svn not available'
	test_done
fi

svnconf=$PWD/svnconf
export svnconf

svn_cmd () {
	subcommand=$1 &&
	shift &&
	mkdir -p "$svnconf" &&
	svn "$subcommand" --config-dir "$svnconf" "$@"
}

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

test_expect_success 't9135/svn.dump' '
	svnadmin create simple-svn &&
	svnadmin load simple-svn <"$TEST_DIRECTORY/t9135/svn.dump" &&
	svn_cmd export "file://$PWD/simple-svn" simple-svnco &&
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
