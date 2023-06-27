#!/bin/sh

test_description='git revert'

. ./test-lib.sh

test_expect_success 'fresh reverts' '
	test_commit --no-tag A file1 &&
	test_commit --no-tag B file1 &&
	git revert --no-edit HEAD &&
	echo "Revert \"B\"" > expect &&
	git log -1 --pretty=%s > actual &&
	test_cmp expect actual &&
	git revert --no-edit HEAD &&
	echo "Reapply \"B\"" > expect &&
	git log -1 --pretty=%s > actual &&
	test_cmp expect actual &&
	git revert --no-edit HEAD &&
	echo "Revert \"Reapply \"B\"\"" > expect &&
	git log -1 --pretty=%s > actual &&
	test_cmp expect actual
'

test_expect_success 'legacy double revert' '
	test_commit --no-tag "Revert \"Revert \"B\"\"" file1 &&
	git revert --no-edit HEAD &&
	echo "Revert \"Reapply \"B\"\"" > expect &&
	git log -1 --pretty=%s > actual &&
	test_cmp expect actual
'

test_done
