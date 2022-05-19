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
     but add . &&
     but cummit -m "first cummit" &&

     echo "new file in subdir 2" > directory2/file2 &&
     but add . &&
     but cummit -m "cummit in directory2" &&

     echo "changed file 1" > file1 &&
     but cummit -a -m "second cummit" &&

     but config --add color.test.slot1 green &&
     but config --add test.string value &&
     but config --add test.dupstring value1 &&
     but config --add test.dupstring value2 &&
     but config --add test.booltrue true &&
     but config --add test.boolfalse no &&
     but config --add test.boolother other &&
     but config --add test.int 2k &&
     but config --add test.path "~/foo" &&
     but config --add test.pathexpanded "$HOME/foo" &&
     but config --add test.pathmulti foo &&
     but config --add test.pathmulti bar
     '

# The external test will outputs its own plan
test_external_has_tap=1

test_external_without_stderr \
    'Perl API' \
    perl "$TEST_DIRECTORY"/t9700/test.pl

test_done
