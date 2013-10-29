#!/bin/sh
#
# Copyright (c) 2008 Lea Wiemann
#

test_description='perl interface (Git.pm)'
. ./test-lib.sh

if ! test_have_prereq PERL; then
	skip_all='skipping perl interface tests, perl not available'
	test_done
fi

perl -MTest::More -e 0 2>/dev/null || {
	skip_all="Perl Test::More unavailable, skipping test"
	test_done
}

# set up test repository

test_expect_success \
    'set up test repository' \
    'echo "test file 1" > file1 &&
     echo "test file 2" > file2 &&
     mkdir directory1 &&
     echo "in directory1" >> directory1/file &&
     mkdir directory2 &&
     echo "in directory2" >> directory2/file &&
     git add . &&
     git commit -m "first commit" &&

     echo "new file in subdir 2" > directory2/file2 &&
     git add . &&
     git commit -m "commit in directory2" &&

     echo "changed file 1" > file1 &&
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

# The external test will outputs its own plan
test_external_has_tap=1

test_external_without_stderr \
    'Perl API' \
    perl "$TEST_DIRECTORY"/t9700/test.pl

test_done
