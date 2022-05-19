#!/bin/sh

test_description='but rebase interactive environment'

. ./test-lib.sh

test_expect_success 'setup' '
	test_cummit one &&
	test_cummit two &&
	test_cummit three
'

test_expect_success 'rebase --exec does not muck with BUT_DIR' '
	but rebase --exec "printf %s \$BUT_DIR >environ" HEAD~1 &&
	test_must_be_empty environ
'

test_expect_success 'rebase --exec does not muck with BUT_WORK_TREE' '
	but rebase --exec "printf %s \$BUT_WORK_TREE >environ" HEAD~1 &&
	test_must_be_empty environ
'

test_done
