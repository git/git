#!/bin/sh

test_description='but add --all'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	(
		echo .butignore &&
		echo will-remove
	) >expect &&
	(
		echo actual &&
		echo expect &&
		echo ignored
	) >.butignore &&
	but --literal-pathspecs add --all &&
	>will-remove &&
	but add --all &&
	test_tick &&
	but cummit -m initial &&
	but ls-files >actual &&
	test_cmp expect actual
'

test_expect_success 'but add --all' '
	(
		echo .butignore &&
		echo not-ignored &&
		echo "M	.butignore" &&
		echo "A	not-ignored" &&
		echo "D	will-remove"
	) >expect &&
	>ignored &&
	>not-ignored &&
	echo modification >>.butignore &&
	rm -f will-remove &&
	but add --all &&
	but update-index --refresh &&
	but ls-files >actual &&
	but diff-index --name-status --cached HEAD >>actual &&
	test_cmp expect actual
'

test_expect_success 'Just "but add" is a no-op' '
	but reset --hard &&
	echo >will-remove &&
	>will-not-be-added &&
	but add &&
	but diff-index --name-status --cached HEAD >actual &&
	test_must_be_empty actual
'

test_done
