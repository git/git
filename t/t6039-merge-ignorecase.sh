#!/bin/sh

test_description='git-merge with case-changing rename on case-insensitive file system'

. ./test-lib.sh

if ! test_have_prereq CASE_INSENSITIVE_FS
then
	skip_all='skipping case insensitive tests - case sensitive file system'
	test_done
fi

test_expect_success 'merge with case-changing rename' '
	test $(git config core.ignorecase) = true &&
	>TestCase &&
	git add TestCase &&
	git commit -m "add TestCase" &&
	git tag baseline
	git checkout -b with-camel &&
	>foo &&
	git add foo &&
	git commit -m "intervening commit" &&
	git checkout master &&
	git rm TestCase &&
	>testcase &&
	git add testcase &&
	git commit -m "rename to testcase" &&
	git checkout with-camel &&
	git merge master -m "merge" &&
	test_path_is_file testcase
'

test_expect_success 'merge with case-changing rename on both sides' '
	git checkout master &&
	git reset --hard baseline &&
	git branch -D with-camel &&
	git checkout -b with-camel &&
	git mv TestCase testcase &&
	git commit -m "recase on branch" &&
	>foo &&
	git add foo &&
	git commit -m "intervening commit" &&
	git checkout master &&
	git rm TestCase &&
	>testcase &&
	git add testcase &&
	git commit -m "rename to testcase" &&
	git checkout with-camel &&
	git merge master -m "merge" &&
	test_path_is_file testcase
'

test_done
