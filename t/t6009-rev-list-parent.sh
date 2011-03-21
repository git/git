#!/bin/sh

test_description='properly cull all ancestors'

. ./test-lib.sh

test_expect_success setup '

	touch file &&
	git add file &&

	test_commit one &&

	test_tick=$(($test_tick - 2400)) &&

	test_commit two &&
	test_commit three &&
	test_commit four &&

	git log --pretty=oneline --abbrev-commit
'

test_expect_success 'one is ancestor of others and should not be shown' '

	git rev-list one --not four >result &&
	>expect &&
	test_cmp expect result

'

test_done
