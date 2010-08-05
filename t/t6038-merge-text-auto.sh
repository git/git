#!/bin/sh

test_description='CRLF merge conflict across text=auto change

* [master] remove .gitattributes
 ! [side] add line from b
--
 + [side] add line from b
*  [master] remove .gitattributes
*  [master^] add line from a
*  [master~2] normalize file
*+ [side^] Initial
'

. ./test-lib.sh

test_expect_success setup '
	git config merge.renormalize true &&
	git config core.autocrlf false &&

	echo first line | append_cr >file &&
	echo first line >control_file &&
	echo only line >inert_file &&

	git add file control_file inert_file &&
	test_tick &&
	git commit -m "Initial" &&
	git tag initial &&
	git branch side &&

	echo "* text=auto" >.gitattributes &&
	touch file &&
	git add .gitattributes file &&
	test_tick &&
	git commit -m "normalize file" &&

	echo same line | append_cr >>file &&
	echo same line >>control_file &&
	git add file control_file &&
	test_tick &&
	git commit -m "add line from a" &&
	git tag a &&

	git rm .gitattributes &&
	rm file &&
	git checkout file &&
	test_tick &&
	git commit -m "remove .gitattributes" &&
	git tag c &&

	git checkout side &&
	echo same line | append_cr >>file &&
	echo same line >>control_file &&
	git add file control_file &&
	test_tick &&
	git commit -m "add line from b" &&
	git tag b &&

	git checkout master
'

test_expect_success 'Merge after setting text=auto' '
	cat <<-\EOF >expected &&
	first line
	same line
	EOF

	git rm -fr . &&
	rm -f .gitattributes &&
	git reset --hard a &&
	git merge b &&
	test_cmp expected file
'

test_expect_success 'Merge addition of text=auto' '
	cat <<-\EOF >expected &&
	first line
	same line
	EOF

	git rm -fr . &&
	rm -f .gitattributes &&
	git reset --hard b &&
	git merge a &&
	test_cmp expected file
'

test_expect_success 'Test delete/normalize conflict' '
	git checkout -f side &&
	git rm -fr . &&
	rm -f .gitattributes &&
	git reset --hard initial &&
	git rm file &&
	git commit -m "remove file" &&
	git checkout master &&
	git reset --hard a^ &&
	git merge side
'

test_done
