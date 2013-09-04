#!/bin/sh

test_description='Test cherry-pick with directory/file conflicts'
. ./test-lib.sh

test_expect_success 'Initialize repository' '
	mkdir a &&
	>a/f &&
	git add a &&
	git commit -m a
'

test_expect_success 'Setup rename across paths each below D/F conflicts' '
	mkdir b &&
	test_ln_s_add ../a b/a &&
	git commit -m b &&

	git checkout -b branch &&
	rm b/a &&
	git mv a b/a &&
	test_ln_s_add b/a a &&
	git commit -m swap &&

	>f1 &&
	git add f1 &&
	git commit -m f1
'

test_expect_success 'Cherry-pick succeeds with rename across D/F conflicts' '
	git reset --hard &&
	git checkout master^0 &&
	git cherry-pick branch
'

test_expect_success 'Setup rename with file on one side matching directory name on other' '
	git checkout --orphan nick-testcase &&
	git rm -rf . &&

	>empty &&
	git add empty &&
	git commit -m "Empty file" &&

	git checkout -b simple &&
	mv empty file &&
	mkdir empty &&
	mv file empty &&
	git add empty/file &&
	git commit -m "Empty file under empty dir" &&

	echo content >newfile &&
	git add newfile &&
	git commit -m "New file"
'

test_expect_success 'Cherry-pick succeeds with was_a_dir/file -> was_a_dir (resolve)' '
	git reset --hard &&
	git checkout -q nick-testcase^0 &&
	git cherry-pick --strategy=resolve simple
'

test_expect_success 'Cherry-pick succeeds with was_a_dir/file -> was_a_dir (recursive)' '
	git reset --hard &&
	git checkout -q nick-testcase^0 &&
	git cherry-pick --strategy=recursive simple
'

test_expect_success 'Setup rename with file on one side matching different dirname on other' '
	git reset --hard &&
	git checkout --orphan mergeme &&
	git rm -rf . &&

	mkdir sub &&
	mkdir othersub &&
	echo content > sub/file &&
	echo foo > othersub/whatever &&
	git add -A &&
	git commit -m "Common commit" &&

	git rm -rf othersub &&
	git mv sub/file othersub &&
	git commit -m "Commit to merge" &&

	git checkout -b newhead mergeme~1 &&
	>independent-change &&
	git add independent-change &&
	git commit -m "Completely unrelated change"
'

test_expect_success 'Cherry-pick with rename to different D/F conflict succeeds (resolve)' '
	git reset --hard &&
	git checkout -q newhead^0 &&
	git cherry-pick --strategy=resolve mergeme
'

test_expect_success 'Cherry-pick with rename to different D/F conflict succeeds (recursive)' '
	git reset --hard &&
	git checkout -q newhead^0 &&
	git cherry-pick --strategy=recursive mergeme
'

test_done
