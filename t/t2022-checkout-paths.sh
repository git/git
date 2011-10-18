#!/bin/sh

test_description='checkout $tree -- $paths'
. ./test-lib.sh

test_expect_success setup '
	mkdir dir &&
	>dir/master &&
	echo common >dir/common &&
	git add dir/master dir/common &&
	test_tick && git commit -m "master has dir/master" &&
	git checkout -b next &&
	git mv dir/master dir/next0 &&
	echo next >dir/next1 &&
	git add dir &&
	test_tick && git commit -m "next has dir/next but not dir/master"
'

test_expect_success 'checking out paths out of a tree does not clobber unrelated paths' '
	git checkout next &&
	git reset --hard &&
	rm dir/next0 &&
	cat dir/common >expect.common &&
	echo modified >expect.next1 &&
	cat expect.next1 >dir/next1 &&
	echo untracked >expect.next2 &&
	cat expect.next2 >dir/next2 &&

	git checkout master dir &&

	test_cmp expect.common dir/common &&
	test_path_is_file dir/master &&
	git diff --exit-code master dir/master &&

	test_path_is_missing dir/next0 &&
	test_cmp expect.next1 dir/next1 &&
	test_path_is_file dir/next2 &&
	test_must_fail git ls-files --error-unmatch dir/next2 &&
	test_cmp expect.next2 dir/next2
'

test_done
