#!/bin/sh

test_description='CRLF merge conflict across text=auto change'

. ./test-lib.sh

test_expect_success setup '
	git config merge.renormalize true &&
	git config core.autocrlf false &&
	echo first line | append_cr >file &&
	echo first line >control_file &&
	echo only line >inert_file &&
	git add file control_file inert_file &&
	git commit -m "Initial" &&
	git tag initial &&
	git branch side &&
	echo "* text=auto" >.gitattributes &&
	touch file &&
	git add .gitattributes file &&
	git commit -m "normalize file" &&
	echo same line | append_cr >>file &&
	echo same line >>control_file &&
	git add file control_file &&
	git commit -m "add line from a" &&
	git tag a &&
	git rm .gitattributes &&
	rm file &&
	git checkout file &&
	git commit -m "remove .gitattributes" &&
	git tag c &&
	git checkout side &&
	echo same line | append_cr >>file &&
	echo same line >>control_file &&
	git add file control_file &&
	git commit -m "add line from b" &&
	git tag b &&
	git checkout master
'

test_expect_success 'Check merging after setting text=auto' '
	git reset --hard a &&
	git merge b &&
	cat file | remove_cr >file.temp &&
	test_cmp file file.temp
'

test_expect_success 'Check merging addition of text=auto' '
	git reset --hard b &&
	git merge a &&
	cat file | remove_cr >file.temp &&
	test_cmp file file.temp
'

test_expect_failure 'Test delete/normalize conflict' '
	git checkout side &&
	git reset --hard initial &&
	git rm file &&
	git commit -m "remove file" &&
	git checkout master &&
	git reset --hard a^ &&
	git merge side
'

test_done
