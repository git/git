#!/bin/sh

test_description='CRLF merge conflict across text=auto change

* [main] remove .butattributes
 ! [side] add line from b
--
 + [side] add line from b
*  [main] remove .butattributes
*  [main^] add line from a
*  [main~2] normalize file
*+ [side^] Initial
'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_have_prereq SED_STRIPS_CR && SED_OPTIONS=-b

compare_files () {
	tr '\015\000' QN <"$1" >"$1".expect &&
	tr '\015\000' QN <"$2" >"$2".actual &&
	test_cmp "$1".expect "$2".actual &&
	rm "$1".expect "$2".actual
}

test_expect_success setup '
	but config core.autocrlf false &&

	echo first line | append_cr >file &&
	echo first line >control_file &&
	echo only line >inert_file &&

	but add file control_file inert_file &&
	test_tick &&
	but cummit -m "Initial" &&
	but tag initial &&
	but branch side &&

	echo "* text=auto" >.butattributes &&
	echo first line >file &&
	but add .butattributes file &&
	test_tick &&
	but cummit -m "normalize file" &&

	echo same line | append_cr >>file &&
	echo same line >>control_file &&
	but add file control_file &&
	test_tick &&
	but cummit -m "add line from a" &&
	but tag a &&

	but rm .butattributes &&
	rm file &&
	but checkout file &&
	test_tick &&
	but cummit -m "remove .butattributes" &&
	but tag c &&

	but checkout side &&
	echo same line | append_cr >>file &&
	echo same line >>control_file &&
	but add file control_file &&
	test_tick &&
	but cummit -m "add line from b" &&
	but tag b &&

	but checkout main
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
	but config merge.renormalize true &&
	but rm -fr . &&
	rm -f .butattributes &&
	but reset --hard a &&
	but merge b &&
	compare_files expected file
'

test_expect_success 'Merge addition of text=auto eol=LF' '
	but config core.eol lf &&
	cat <<-\EOF >expected &&
	first line
	same line
	EOF

	but config merge.renormalize true &&
	but rm -fr . &&
	rm -f .butattributes &&
	but reset --hard b &&
	but merge a &&
	compare_files  expected file
'

test_expect_success 'Merge addition of text=auto eol=CRLF' '
	but config core.eol crlf &&
	cat <<-\EOF >expected &&
	first line
	same line
	EOF

	append_cr <expected >expected.temp &&
	mv expected.temp expected &&
	but config merge.renormalize true &&
	but rm -fr . &&
	rm -f .butattributes &&
	but reset --hard b &&
	echo >&2 "After but reset --hard b" &&
	but ls-files -s --eol >&2 &&
	but merge a &&
	compare_files  expected file
'

test_expect_success 'Detect CRLF/LF conflict after setting text=auto' '
	but config core.eol native &&
	echo "<<<<<<<" >expected &&
	echo first line >>expected &&
	echo same line >>expected &&
	echo ======= >>expected &&
	echo first line | append_cr >>expected &&
	echo same line | append_cr >>expected &&
	echo ">>>>>>>" >>expected &&
	but config merge.renormalize false &&
	rm -f .butattributes &&
	but reset --hard a &&
	test_must_fail but merge b &&
	fuzz_conflict file >file.fuzzy &&
	compare_files expected file.fuzzy
'

test_expect_success 'Detect LF/CRLF conflict from addition of text=auto' '
	echo "<<<<<<<" >expected &&
	echo first line | append_cr >>expected &&
	echo same line | append_cr >>expected &&
	echo ======= >>expected &&
	echo first line >>expected &&
	echo same line >>expected &&
	echo ">>>>>>>" >>expected &&
	but config merge.renormalize false &&
	rm -f .butattributes &&
	but reset --hard b &&
	test_must_fail but merge a &&
	fuzz_conflict file >file.fuzzy &&
	compare_files expected file.fuzzy
'

test_expect_success 'checkout -m after setting text=auto' '
	cat <<-\EOF >expected &&
	first line
	same line
	EOF

	but config merge.renormalize true &&
	but rm -fr . &&
	rm -f .butattributes &&
	but reset --hard initial &&
	but restore --source=a -- . &&
	but checkout -m b &&
	but diff --no-index --ignore-cr-at-eol expected file
'

test_expect_success 'checkout -m addition of text=auto' '
	cat <<-\EOF >expected &&
	first line
	same line
	EOF

	but config merge.renormalize true &&
	but rm -fr . &&
	rm -f .butattributes file &&
	but reset --hard initial &&
	but restore --source=b -- . &&
	but checkout -m a &&
	but diff --no-index --ignore-cr-at-eol expected file
'

test_expect_success 'Test delete/normalize conflict' '
	but checkout -f side &&
	but rm -fr . &&
	rm -f .butattributes &&
	but reset --hard initial &&
	but rm file &&
	but cummit -m "remove file" &&
	but checkout main &&
	but reset --hard a^ &&
	but merge side &&
	test_path_is_missing file
'

test_expect_success 'rename/delete vs. renormalization' '
	but init subrepo &&
	(
		cd subrepo &&
		echo foo >oldfile &&
		but add oldfile &&
		but cummit -m original &&

		but branch rename &&
		but branch nuke &&

		but checkout rename &&
		but mv oldfile newfile &&
		but cummit -m renamed &&

		but checkout nuke &&
		but rm oldfile &&
		but cummit -m deleted &&

		but checkout rename^0 &&
		test_must_fail but -c merge.renormalize=true merge nuke >out &&

		grep "rename/delete" out
	)
'

test_done
