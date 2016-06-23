#!/bin/sh

test_description='watchman extension smoke tests'

# These don't actually test watchman interaction -- just the
# index extension

. ./test-lib.sh

test_expect_success 'enable watchman' '
	test_commit a &&
	test-dump-watchman .git/index >actual &&
	echo "last_update: (null)" >expect &&
	test_cmp expect actual &&
	git update-index --watchman &&
	test-dump-watchman .git/index >actual &&
	echo "last_update: " >expect &&
	test_cmp expect actual
'

test_expect_success 'disable watchman' '
	git update-index --no-watchman &&
	test-dump-watchman .git/index >actual &&
	echo "last_update: (null)" >expect &&
	test_cmp expect actual
'

test_expect_success 'auto-enable watchman' '
	test_config index.addwatchmanextension true &&
	test_commit c &&
	test-dump-watchman .git/index >actual &&
	echo "last_update: " >expect &&
	test_cmp expect actual
'


test_done
