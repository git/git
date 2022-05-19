#!/bin/sh

test_description='but apply --numstat - <patch'


TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	echo hello >text &&
	but add text &&
	echo goodbye >text &&
	but diff >patch
'

test_expect_success 'but apply --numstat - < patch' '
	echo "1	1	text" >expect &&
	but apply --numstat - <patch >actual &&
	test_cmp expect actual
'

test_expect_success 'but apply --numstat - < patch patch' '
	cat >expect <<-\EOF &&
	1	1	text
	1	1	text
	EOF
	but apply --numstat - < patch patch >actual &&
	test_cmp expect actual
'

test_done
