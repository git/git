#!/bin/sh

test_description='test am --quoted-cr=<action>'

. ./test-lib.sh

DATA="$TEST_DIRECTORY/t4258"

test_expect_success 'setup' '
	test_write_lines one two three >text &&
	test_cummit one text &&
	test_write_lines one owt three >text &&
	test_cummit two text
'

test_expect_success 'am warn if quoted-cr is found' '
	but reset --hard one &&
	test_must_fail but am "$DATA/mbox" 2>err &&
	grep "quoted CRLF detected" err
'

test_expect_success 'am --quoted-cr=strip' '
	test_might_fail but am --abort &&
	but reset --hard one &&
	but am --quoted-cr=strip "$DATA/mbox" &&
	but diff --exit-code HEAD two
'

test_expect_success 'am with config mailinfo.quotedCr=strip' '
	test_might_fail but am --abort &&
	but reset --hard one &&
	test_config mailinfo.quotedCr strip &&
	but am "$DATA/mbox" &&
	but diff --exit-code HEAD two
'

test_done
