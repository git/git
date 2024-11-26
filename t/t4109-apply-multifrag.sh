#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
# Copyright (c) 2005 Robert Fitzsimons
#

test_description='git apply test patches with multiple fragments.'


. ./test-lib.sh

cp "$TEST_DIRECTORY/t4109/patch1.patch" .
cp "$TEST_DIRECTORY/t4109/patch2.patch" .
cp "$TEST_DIRECTORY/t4109/patch3.patch" .
cp "$TEST_DIRECTORY/t4109/patch4.patch" .

test_expect_success 'git apply (1)' '
	git apply patch1.patch patch2.patch &&
	test_cmp "$TEST_DIRECTORY/t4109/expect-1" main.c
'
rm -f main.c

test_expect_success 'git apply (2)' '
	git apply patch1.patch patch2.patch patch3.patch &&
	test_cmp "$TEST_DIRECTORY/t4109/expect-2" main.c
'
rm -f main.c

test_expect_success 'git apply (3)' '
	git apply patch1.patch patch4.patch &&
	test_cmp "$TEST_DIRECTORY/t4109/expect-3" main.c
'
mv main.c main.c.git

test_done

