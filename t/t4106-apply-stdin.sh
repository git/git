#!/bin/sh

test_description='git apply --numstat - <patch'


TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	echo hello >text &&
	git add text &&
	echo goodbye >text &&
	git diff >patch
'

test_expect_success 'git apply --numstat - < patch' '
	echo "1	1	text" >expect &&
	git apply --numstat - <patch >actual &&
	test_cmp expect actual
'

test_expect_success 'git apply --numstat - < patch patch' '
	cat >expect <<-\EOF &&
	1	1	text
	1	1	text
	EOF
	git apply --numstat - < patch patch >actual &&
	test_cmp expect actual
'

test_done
