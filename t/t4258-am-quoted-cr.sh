#!/bin/sh

test_description='test am --quoted-cr=<action>'

. ./test-lib.sh

DATA="$TEST_DIRECTORY/t4258"

test_expect_success 'setup' '
	test_write_lines one two three >text &&
	test_commit one text &&
	test_write_lines one owt three >text &&
	test_commit two text
'

test_expect_success 'am warn if quoted-cr is found' '
	git reset --hard one &&
	test_must_fail git am "$DATA/mbox" 2>err &&
	grep "quoted CRLF detected" err
'

test_expect_success 'am --quoted-cr=strip' '
	test_might_fail git am --abort &&
	git reset --hard one &&
	git am --quoted-cr=strip "$DATA/mbox" &&
	git diff --exit-code HEAD two
'

test_expect_success 'am with config mailinfo.quotedCr=strip' '
	test_might_fail git am --abort &&
	git reset --hard one &&
	test_config mailinfo.quotedCr strip &&
	git am "$DATA/mbox" &&
	git diff --exit-code HEAD two
'

test_done
