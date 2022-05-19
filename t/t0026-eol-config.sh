#!/bin/sh

test_description='CRLF conversion'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

has_cr() {
	tr '\015' Q <"$1" | grep Q >/dev/null
}

test_expect_success setup '

	but config core.autocrlf false &&

	echo "one text" > .butattributes &&

	test_write_lines Hello world how are you >one &&
	test_write_lines I am very very fine thank you >two &&
	but add . &&

	but cummit -m initial &&

	one=$(but rev-parse HEAD:one) &&
	two=$(but rev-parse HEAD:two) &&

	echo happy.
'

test_expect_success 'eol=lf puts LFs in normalized file' '

	rm -f .butattributes tmp one two &&
	but config core.eol lf &&
	but read-tree --reset -u HEAD &&

	! has_cr one &&
	! has_cr two &&
	onediff=$(but diff one) &&
	twodiff=$(but diff two) &&
	test -z "$onediff" && test -z "$twodiff"
'

test_expect_success 'eol=crlf puts CRLFs in normalized file' '

	rm -f .butattributes tmp one two &&
	but config core.eol crlf &&
	but read-tree --reset -u HEAD &&

	has_cr one &&
	! has_cr two &&
	onediff=$(but diff one) &&
	twodiff=$(but diff two) &&
	test -z "$onediff" && test -z "$twodiff"
'

test_expect_success 'autocrlf=true overrides eol=lf' '

	rm -f .butattributes tmp one two &&
	but config core.eol lf &&
	but config core.autocrlf true &&
	but read-tree --reset -u HEAD &&

	has_cr one &&
	has_cr two &&
	onediff=$(but diff one) &&
	twodiff=$(but diff two) &&
	test -z "$onediff" && test -z "$twodiff"
'

test_expect_success 'autocrlf=true overrides unset eol' '

	rm -f .butattributes tmp one two &&
	but config --unset-all core.eol &&
	but config core.autocrlf true &&
	but read-tree --reset -u HEAD &&

	has_cr one &&
	has_cr two &&
	onediff=$(but diff one) &&
	twodiff=$(but diff two) &&
	test -z "$onediff" && test -z "$twodiff"
'

test_expect_success NATIVE_CRLF 'eol native is crlf' '

	rm -rf native_eol && mkdir native_eol &&
	(
		cd native_eol &&
		printf "*.txt text\n" >.butattributes &&
		printf "one\r\ntwo\r\nthree\r\n" >filedos.txt &&
		printf "one\ntwo\nthree\n" >fileunix.txt &&
		but init &&
		but config core.autocrlf false &&
		but config core.eol native &&
		but add filedos.txt fileunix.txt &&
		but cummit -m "first" &&
		rm file*.txt &&
		but reset --hard HEAD &&
		has_cr filedos.txt &&
		has_cr fileunix.txt
	)
'

test_done
