#!/bin/sh

test_description='CRLF conversion'

. ./test-lib.sh

has_cr() {
	tr '\015' Q <"$1" | grep Q >/dev/null
}

test_expect_success setup '

	git config core.autocrlf false &&

	echo "one text" > .gitattributes &&

	for w in Hello world how are you; do echo $w; done >one &&
	for w in I am very very fine thank you; do echo $w; done >two &&
	git add . &&

	git commit -m initial &&

	one=`git rev-parse HEAD:one` &&
	two=`git rev-parse HEAD:two` &&

	echo happy.
'

test_expect_success 'eol=lf puts LFs in normalized file' '

	rm -f .gitattributes tmp one two &&
	git config core.eol lf &&
	git read-tree --reset -u HEAD &&

	! has_cr one &&
	! has_cr two &&
	onediff=`git diff one` &&
	twodiff=`git diff two` &&
	test -z "$onediff" -a -z "$twodiff"
'

test_expect_success 'eol=crlf puts CRLFs in normalized file' '

	rm -f .gitattributes tmp one two &&
	git config core.eol crlf &&
	git read-tree --reset -u HEAD &&

	has_cr one &&
	! has_cr two &&
	onediff=`git diff one` &&
	twodiff=`git diff two` &&
	test -z "$onediff" -a -z "$twodiff"
'

test_expect_success 'autocrlf=true overrides eol=lf' '

	rm -f .gitattributes tmp one two &&
	git config core.eol lf &&
	git config core.autocrlf true &&
	git read-tree --reset -u HEAD &&

	has_cr one &&
	has_cr two &&
	onediff=`git diff one` &&
	twodiff=`git diff two` &&
	test -z "$onediff" -a -z "$twodiff"
'

test_expect_success 'autocrlf=true overrides unset eol' '

	rm -f .gitattributes tmp one two &&
	git config --unset-all core.eol &&
	git config core.autocrlf true &&
	git read-tree --reset -u HEAD &&

	has_cr one &&
	has_cr two &&
	onediff=`git diff one` &&
	twodiff=`git diff two` &&
	test -z "$onediff" -a -z "$twodiff"
'

test_done
