#!/bin/sh

test_description='git add --all'

. ./test-lib.sh

test_expect_success setup '
	(
		echo .gitignore
		echo will-remove
	) >expect &&
	(
		echo actual
		echo expect
		echo ignored
	) >.gitignore &&
	>will-remove &&
	git add --all &&
	test_tick &&
	git commit -m initial &&
	git ls-files >actual &&
	test_cmp expect actual
'

test_expect_success 'git add --all' '
	(
		echo .gitignore
		echo not-ignored
		echo "M	.gitignore"
		echo "A	not-ignored"
		echo "D	will-remove"
	) >expect &&
	>ignored &&
	>not-ignored &&
	echo modification >>.gitignore &&
	rm -f will-remove &&
	git add --all &&
	git update-index --refresh &&
	git ls-files >actual &&
	git diff-index --name-status --cached HEAD >>actual &&
	test_cmp expect actual
'

test_done
