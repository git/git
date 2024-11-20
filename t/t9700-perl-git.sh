#!/bin/sh
#
# Copyright (c) 2008 Lea Wiemann
#

test_description='perl interface (Git.pm)'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-perl.sh

skip_all_if_no_Test_More

# set up test repository

test_expect_success 'set up test repository' '
	echo "test file 1" >file1 &&
	echo "test file 2" >file2 &&
	mkdir directory1 &&
	echo "in directory1" >>directory1/file &&
	mkdir directory2 &&
	echo "in directory2" >>directory2/file &&
	git add . &&
	git commit -m "first commit" &&

	echo "new file in subdir 2" >directory2/file2 &&
	git add . &&
	git commit -m "commit in directory2" &&

	echo "changed file 1" >file1 &&
	git commit -a -m "second commit" &&

	git config --add color.test.slot1 green &&
	git config --add test.string value &&
	git config --add test.dupstring value1 &&
	git config --add test.dupstring value2 &&
	git config --add test.booltrue true &&
	git config --add test.boolfalse no &&
	git config --add test.boolother other &&
	git config --add test.int 2k &&
	git config --add test.path "~/foo" &&
	git config --add test.pathexpanded "$HOME/foo" &&
	git config --add test.pathmulti foo &&
	git config --add test.pathmulti bar
'

test_expect_success 'set up bare repository' '
	git init --bare bare.git &&
	git -C bare.git --work-tree=. commit --allow-empty -m "bare commit"
'

test_expect_success 'use t9700/test.pl to test Git.pm' '
	"$PERL_PATH" "$TEST_DIRECTORY"/t9700/test.pl 2>stderr &&
	test_must_be_empty stderr
'

test_done
