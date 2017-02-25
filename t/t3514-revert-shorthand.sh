#!/bin/sh

test_description='log can show previous branch using shorthand - for @{-1}'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit first
'

test_expect_success 'setup branches' '
	echo "hello" >hello &&
	cat hello >expect &&
	git add hello &&
	git commit -m "hello first commit" &&
	echo "world" >>hello &&
	git commit -am "hello second commit" &&
	git checkout -b testing-1 &&
	git checkout master &&
	git revert --no-edit - &&
	cat hello >actual &&
	test_cmp expect actual
'

test_done
