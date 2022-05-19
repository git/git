#!/bin/sh

test_description='am --interactive tests'
. ./test-lib.sh

test_expect_success 'set up patches to apply' '
	test_cummit unrelated &&
	test_cummit no-conflict &&
	test_cummit conflict-patch file patch &&
	but format-patch --stdout -2 >mbox &&

	but reset --hard unrelated &&
	test_cummit conflict-main file main base
'

# Sanity check our setup.
test_expect_success 'applying all patches generates conflict' '
	test_must_fail but am mbox &&
	echo resolved >file &&
	but add -u &&
	but am --resolved
'

test_expect_success 'interactive am can apply a single patch' '
	but reset --hard base &&
	# apply the first, but not the second
	test_write_lines y n | but am -i mbox &&

	echo no-conflict >expect &&
	but log -1 --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'interactive am can resolve conflict' '
	but reset --hard base &&
	# apply both; the second one will conflict
	test_write_lines y y | test_must_fail but am -i mbox &&
	echo resolved >file &&
	but add -u &&
	# interactive "--resolved" will ask us if we want to apply the result
	echo y | but am -i --resolved &&

	echo conflict-patch >expect &&
	but log -1 --format=%s >actual &&
	test_cmp expect actual &&

	echo resolved >expect &&
	but cat-file blob HEAD:file >actual &&
	test_cmp expect actual
'

test_done
