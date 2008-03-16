#!/bin/sh

test_description='properly cull all ancestors'

. ./test-lib.sh

commit () {
	test_tick &&
	echo $1 >file &&
	git commit -a -m $1 &&
	git tag $1
}

test_expect_success setup '

	touch file &&
	git add file &&

	commit one &&

	test_tick=$(($test_tick - 2400))

	commit two &&
	commit three &&
	commit four &&

	git log --pretty=oneline --abbrev-commit
'

test_expect_failure 'one is ancestor of others and should not be shown' '

	git rev-list one --not four >result &&
	>expect &&
	test_cmp expect result

'

test_done
