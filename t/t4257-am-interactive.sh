#!/bin/sh

test_description='am --interactive tests'

. ./test-lib.sh

test_expect_success 'set up patches to apply' '
	test_commit unrelated &&
	test_commit no-conflict &&
	test_commit conflict-patch file patch &&
	git format-patch --stdout -2 >mbox &&

	git reset --hard unrelated &&
	test_commit conflict-main file main base
'

# Sanity check our setup.
test_expect_success 'applying all patches generates conflict' '
	test_must_fail git am mbox &&
	echo resolved >file &&
	git add -u &&
	git am --resolved
'

test_expect_success 'interactive am can apply a single patch' '
	git reset --hard base &&
	# apply the first, but not the second
	test_write_lines y n | git am -i mbox &&

	echo no-conflict >expect &&
	git log -1 --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'interactive am can resolve conflict' '
	git reset --hard base &&
	# apply both; the second one will conflict
	test_write_lines y y | test_must_fail git am -i mbox &&
	echo resolved >file &&
	git add -u &&
	# interactive "--resolved" will ask us if we want to apply the result
	echo y | git am -i --resolved &&

	echo conflict-patch >expect &&
	git log -1 --format=%s >actual &&
	test_cmp expect actual &&

	echo resolved >expect &&
	git cat-file blob HEAD:file >actual &&
	test_cmp expect actual
'

test_done
