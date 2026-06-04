#!/bin/sh

test_description='CRLF conversion'

. ./test-lib.sh

has_cr() {
	tr '\015' Q <"$1" | grep Q >/dev/null
}

test_expect_success setup '

	git config core.autocrlf false &&

	echo "one text" > .gitattributes &&

	test_write_lines Hello world how are you >one &&
	test_write_lines I am very very fine thank you >two &&
	git add . &&

	git commit -m initial &&

	one=$(git rev-parse HEAD:one) &&
	two=$(git rev-parse HEAD:two) &&

	echo happy.
'

test_expect_success 'eol=lf puts LFs in normalized file' '

	rm -f .gitattributes tmp one two &&
	git config core.eol lf &&
	git read-tree --reset -u HEAD &&

	! has_cr one &&
	! has_cr two &&
	onediff=$(git diff one) &&
	twodiff=$(git diff two) &&
	test -z "$onediff" && test -z "$twodiff"
'

test_expect_success 'eol=crlf puts CRLFs in normalized file' '

	rm -f .gitattributes tmp one two &&
	git config core.eol crlf &&
	git read-tree --reset -u HEAD &&

	has_cr one &&
	! has_cr two &&
	onediff=$(git diff one) &&
	twodiff=$(git diff two) &&
	test -z "$onediff" && test -z "$twodiff"
'

test_expect_success 'autocrlf=true overrides eol=lf' '

	rm -f .gitattributes tmp one two &&
	git config core.eol lf &&
	git config core.autocrlf true &&
	git read-tree --reset -u HEAD &&

	has_cr one &&
	has_cr two &&
	onediff=$(git diff one) &&
	twodiff=$(git diff two) &&
	test -z "$onediff" && test -z "$twodiff"
'

test_expect_success 'autocrlf=true overrides unset eol' '

	rm -f .gitattributes tmp one two &&
	git config --unset-all core.eol &&
	git config core.autocrlf true &&
	git read-tree --reset -u HEAD &&

	has_cr one &&
	has_cr two &&
	onediff=$(git diff one) &&
	twodiff=$(git diff two) &&
	test -z "$onediff" && test -z "$twodiff"
'

test_expect_success NATIVE_CRLF 'eol native is crlf' '

	rm -rf native_eol && mkdir native_eol &&
	(
		cd native_eol &&
		printf "*.txt text\n" >.gitattributes &&
		printf "one\r\ntwo\r\nthree\r\n" >filedos.txt &&
		printf "one\ntwo\nthree\n" >fileunix.txt &&
		git init &&
		git config core.autocrlf false &&
		git config core.eol native &&
		git add filedos.txt fileunix.txt &&
		git commit -m "first" &&
		rm file*.txt &&
		git reset --hard HEAD &&
		has_cr filedos.txt &&
		has_cr fileunix.txt
	)
'

test_done
