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

test_have_prereq SED_STRIPS_CR && SED_OPTIONS=-b

test_expect_success setup '
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

test_expect_success 'set up fuzz_conflict() helper' '
	fuzz_conflict() {
		sed $SED_OPTIONS -e "s/^\([<>=]......\) .*/\1/" "$@"
	}
'

test_expect_success 'Merge after setting text=auto' '
	cat <<-\EOF >expected &&
	first line
	same line
	EOF

	if test_have_prereq NATIVE_CRLF; then
		append_cr <expected >expected.temp &&
		mv expected.temp expected
	fi &&
	git config merge.renormalize true &&
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

	if test_have_prereq NATIVE_CRLF; then
		append_cr <expected >expected.temp &&
		mv expected.temp expected
	fi &&
	git config merge.renormalize true &&
	git rm -fr . &&
	rm -f .gitattributes &&
	git reset --hard b &&
	git merge a &&
	test_cmp expected file
'

test_expect_success 'Detect CRLF/LF conflict after setting text=auto' '
	echo "<<<<<<<" >expected &&
	if test_have_prereq NATIVE_CRLF; then
		echo first line | append_cr >>expected &&
		echo same line | append_cr >>expected &&
		echo ======= | append_cr >>expected
	else
		echo first line >>expected &&
		echo same line >>expected &&
		echo ======= >>expected
	fi &&
	echo first line | append_cr >>expected &&
	echo same line | append_cr >>expected &&
	echo ">>>>>>>" >>expected &&
	git config merge.renormalize false &&
	rm -f .gitattributes &&
	git reset --hard a &&
	test_must_fail git merge b &&
	fuzz_conflict file >file.fuzzy &&
	test_cmp expected file.fuzzy
'

test_expect_success 'Detect LF/CRLF conflict from addition of text=auto' '
	echo "<<<<<<<" >expected &&
	echo first line | append_cr >>expected &&
	echo same line | append_cr >>expected &&
	if test_have_prereq NATIVE_CRLF; then
		echo ======= | append_cr >>expected &&
		echo first line | append_cr >>expected &&
		echo same line | append_cr >>expected
	else
		echo ======= >>expected &&
		echo first line >>expected &&
		echo same line >>expected
	fi &&
	echo ">>>>>>>" >>expected &&
	git config merge.renormalize false &&
	rm -f .gitattributes &&
	git reset --hard b &&
	test_must_fail git merge a &&
	fuzz_conflict file >file.fuzzy &&
	test_cmp expected file.fuzzy
'

test_expect_failure 'checkout -m after setting text=auto' '
	cat <<-\EOF >expected &&
	first line
	same line
	EOF

	git config merge.renormalize true &&
	git rm -fr . &&
	rm -f .gitattributes &&
	git reset --hard initial &&
	git checkout a -- . &&
	git checkout -m b &&
	test_cmp expected file
'

test_expect_failure 'checkout -m addition of text=auto' '
	cat <<-\EOF >expected &&
	first line
	same line
	EOF

	git config merge.renormalize true &&
	git rm -fr . &&
	rm -f .gitattributes file &&
	git reset --hard initial &&
	git checkout b -- . &&
	git checkout -m a &&
	test_cmp expected file
'

test_expect_failure 'cherry-pick patch from after text=auto was added' '
	append_cr <<-\EOF >expected &&
	first line
	same line
	EOF

	git config merge.renormalize true &&
	git rm -fr . &&
	git reset --hard b &&
	test_must_fail git cherry-pick a >err 2>&1 &&
	grep "[Nn]othing added" err &&
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
