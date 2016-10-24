#!/bin/sh

test_description='git reset with paths'

. ./test-lib.sh

test_expect_success setup '
	echo one >file &&
	git add file &&
	git commit -m "one" &&
	git tag initial &&

	echo two >file &&
	git commit -a -m "two" &&

	git checkout -b side initial &&
	echo three >file &&
	git commit -a -m "three"
'

test_expect_success "cause conflict, resolve, and unresolve" '
	git reset --hard &&
	git checkout master &&
	test_must_fail git merge side &&

	git ls-files -u >expect &&

	echo four >file &&
	git add file &&

	git reset --unmerge -- file &&
	git ls-files -u >actual &&
	test_cmp expect actual
'

test_done
