#!/bin/sh

test_description='Merge-recursive rename/delete conflict message'
. ./test-lib.sh

test_expect_success 'rename/delete' '
	echo foo >A &&
	git add A &&
	git commit -m "initial" &&

	git checkout -b rename &&
	git mv A B &&
	git commit -m "rename" &&

	git checkout master &&
	git rm A &&
	git commit -m "delete" &&

	test_must_fail git merge --strategy=recursive rename >output &&
	test_i18ngrep "CONFLICT (rename/delete): A.* renamed .*to B.* in rename" output &&
	test_i18ngrep "CONFLICT (rename/delete): A.*deleted in HEAD." output
'

test_done
