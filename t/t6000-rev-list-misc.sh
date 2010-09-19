#!/bin/sh

test_description='miscellaneous rev-list tests'

. ./test-lib.sh

test_expect_success setup '
	echo content1 >wanted_file &&
	echo content2 >unwanted_file &&
	git add wanted_file unwanted_file &&
	git commit -m one
'

test_expect_success 'rev-list --objects heeds pathspecs' '
	git rev-list --objects HEAD -- wanted_file >output &&
	grep wanted_file output &&
	! grep unwanted_file output
'

test_expect_success 'rev-list --objects with pathspecs and deeper paths' '
	mkdir foo &&
	>foo/file &&
	git add foo/file &&
	git commit -m two &&

	git rev-list --objects HEAD -- foo >output &&
	grep foo/file output &&

	git rev-list --objects HEAD -- foo/file >output &&
	grep foo/file output &&
	! grep unwanted_file output
'

test_expect_success 'rev-list --objects with pathspecs and copied files' '
	git checkout --orphan junio-testcase &&
	git rm -rf . &&

	mkdir two &&
	echo frotz >one &&
	cp one two/three &&
	git add one two/three &&
	test_tick &&
	git commit -m that &&

	ONE=$(git rev-parse HEAD:one)
	git rev-list --objects HEAD two >output &&
	grep "$ONE two/three" output &&
	! grep one output
'

test_done
