#!/bin/sh

test_description='test format=flowed support of but am'

. ./test-lib.sh

test_expect_success 'setup' '
	cp "$TEST_DIRECTORY/t4256/1/mailinfo.c.orig" mailinfo.c &&
	but add mailinfo.c &&
	but cummit -m initial
'

test_expect_success 'am with format=flowed' '
	but am <"$TEST_DIRECTORY/t4256/1/patch" 2>stderr &&
	test_i18ngrep "warning: Patch sent with format=flowed" stderr &&
	test_cmp "$TEST_DIRECTORY/t4256/1/mailinfo.c" mailinfo.c
'

test_done
