#!/bin/sh

test_description='Test cherry-pick with directory/file conflicts'
. ./test-lib.sh

test_expect_success 'Setup rename across paths each below D/F conflicts' '
	mkdir a &&
	>a/f &&
	git add a &&
	git commit -m a &&

	mkdir b &&
	ln -s ../a b/a &&
	git add b &&
	git commit -m b &&

	git checkout -b branch &&
	rm b/a &&
	mv a b/a &&
	ln -s b/a a &&
	git add . &&
	git commit -m swap &&

	>f1 &&
	git add f1 &&
	git commit -m f1
'

test_expect_failure 'Cherry-pick succeeds with rename across D/F conflicts' '
	git reset --hard &&
	git checkout master^0 &&
	git cherry-pick branch
'

test_done
