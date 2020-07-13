#!/bin/sh
#
# Copyright (c) 2019 Rohit Ashiwal
#

test_description='tests to ensure compatibility between am and interactive backends'

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

# This is a special case in which both am and interactive backends
# provide the same output. It was done intentionally because
# both the backends fall short of optimal behaviour.
test_expect_success 'setup' '
	git checkout -b topic &&
	test_write_lines "line 1" "	line 2" "line 3" >file &&
	git add file &&
	git commit -m "add file" &&

	test_write_lines "line 1" "new line 2" "line 3" >file &&
	git commit -am "update file" &&
	git tag side &&

	git checkout --orphan master &&
	test_write_lines "line 1" "        line 2" "line 3" >file &&
	git commit -am "add file" &&
	git tag main
'

test_expect_success '--ignore-whitespace works with apply backend' '
	test_must_fail git rebase --apply main side &&
	git rebase --abort &&
	git rebase --apply --ignore-whitespace main side &&
	git diff --exit-code side
'

test_expect_success '--ignore-whitespace works with merge backend' '
	test_must_fail git rebase --merge main side &&
	git rebase --abort &&
	git rebase --merge --ignore-whitespace main side &&
	git diff --exit-code side
'

test_expect_success '--ignore-whitespace is remembered when continuing' '
	(
		set_fake_editor &&
		FAKE_LINES="break 1" git rebase -i --ignore-whitespace \
			main side &&
		git rebase --continue
	) &&
	git diff --exit-code side
'

# This must be the last test in this file
test_expect_success '$EDITOR and friends are unchanged' '
	test_editor_unchanged
'

test_done
