#!/bin/sh

test_description='git ls-remote'

. ./test-lib.sh

test_expect_success setup '

	>file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	git tag mark &&
	git show-ref --tags -d | sed -e "s/ /	/" >expected.tag &&
	(
		echo "$(git rev-parse HEAD)	HEAD"
		git show-ref -d	| sed -e "s/ /	/"
	) >expected.all &&

	git remote add self $(pwd)/.git

'

test_expect_success 'ls-remote --tags .git' '

	git ls-remote --tags .git >actual &&
	diff -u expected.tag actual

'

test_expect_success 'ls-remote .git' '

	git ls-remote .git >actual &&
	diff -u expected.all actual

'

test_expect_success 'ls-remote --tags self' '

	git ls-remote --tags self >actual &&
	diff -u expected.tag actual

'

test_expect_success 'ls-remote self' '

	git ls-remote self >actual &&
	diff -u expected.all actual

'

test_done
