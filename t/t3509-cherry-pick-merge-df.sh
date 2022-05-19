#!/bin/sh

test_description='Test cherry-pick with directory/file conflicts'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'Initialize repository' '
	mkdir a &&
	>a/f &&
	but add a &&
	but cummit -m a
'

test_expect_success 'Setup rename across paths each below D/F conflicts' '
	mkdir b &&
	test_ln_s_add ../a b/a &&
	but cummit -m b &&

	but checkout -b branch &&
	rm b/a &&
	but mv a b/a &&
	test_ln_s_add b/a a &&
	but cummit -m swap &&

	>f1 &&
	but add f1 &&
	but cummit -m f1
'

test_expect_success 'Cherry-pick succeeds with rename across D/F conflicts' '
	but reset --hard &&
	but checkout main^0 &&
	but cherry-pick branch
'

test_expect_success 'Setup rename with file on one side matching directory name on other' '
	but checkout --orphan nick-testcase &&
	but rm -rf . &&

	>empty &&
	but add empty &&
	but cummit -m "Empty file" &&

	but checkout -b simple &&
	mv empty file &&
	mkdir empty &&
	mv file empty &&
	but add empty/file &&
	but cummit -m "Empty file under empty dir" &&

	echo content >newfile &&
	but add newfile &&
	but cummit -m "New file"
'

test_expect_success 'Cherry-pick succeeds with was_a_dir/file -> was_a_dir (resolve)' '
	but reset --hard &&
	but checkout -q nick-testcase^0 &&
	but cherry-pick --strategy=resolve simple
'

test_expect_success 'Cherry-pick succeeds with was_a_dir/file -> was_a_dir (recursive)' '
	but reset --hard &&
	but checkout -q nick-testcase^0 &&
	but cherry-pick --strategy=recursive simple
'

test_expect_success 'Setup rename with file on one side matching different dirname on other' '
	but reset --hard &&
	but checkout --orphan mergeme &&
	but rm -rf . &&

	mkdir sub &&
	mkdir othersub &&
	echo content > sub/file &&
	echo foo > othersub/whatever &&
	but add -A &&
	but cummit -m "Common cummit" &&

	but rm -rf othersub &&
	but mv sub/file othersub &&
	but cummit -m "cummit to merge" &&

	but checkout -b newhead mergeme~1 &&
	>independent-change &&
	but add independent-change &&
	but cummit -m "Completely unrelated change"
'

test_expect_success 'Cherry-pick with rename to different D/F conflict succeeds (resolve)' '
	but reset --hard &&
	but checkout -q newhead^0 &&
	but cherry-pick --strategy=resolve mergeme
'

test_expect_success 'Cherry-pick with rename to different D/F conflict succeeds (recursive)' '
	but reset --hard &&
	but checkout -q newhead^0 &&
	but cherry-pick --strategy=recursive mergeme
'

test_done
