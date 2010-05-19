#!/bin/sh

test_description='CRLF conversion'

. ./test-lib.sh

has_cr() {
	tr '\015' Q <"$1" | grep Q >/dev/null
}

test_expect_success setup '

	git config core.autocrlf false &&

	for w in Hello world how are you; do echo $w; done >one &&
	for w in I am very very fine thank you; do echo ${w}Q; done | q_to_cr >two &&
	for w in Oh here is a QNUL byte how alarming; do echo ${w}; done | q_to_nul >three &&
	git add . &&

	git commit -m initial &&

	one=`git rev-parse HEAD:one` &&
	two=`git rev-parse HEAD:two` &&
	three=`git rev-parse HEAD:three` &&

	echo happy.
'

test_expect_success 'default settings cause no changes' '

	rm -f .gitattributes tmp one two three &&
	git read-tree --reset -u HEAD &&

	! has_cr one &&
	has_cr two &&
	onediff=`git diff one` &&
	twodiff=`git diff two` &&
	threediff=`git diff three` &&
	test -z "$onediff" -a -z "$twodiff" -a -z "$threediff"
'

test_expect_success 'crlf=true causes a CRLF file to be normalized' '

	# Backwards compatibility check
	rm -f .gitattributes tmp one two three &&
	echo "two crlf" > .gitattributes &&
	git read-tree --reset -u HEAD &&

	# Note, "normalized" means that git will normalize it if added
	has_cr two &&
	twodiff=`git diff two` &&
	test -n "$twodiff"
'

test_expect_success 'text=true causes a CRLF file to be normalized' '

	rm -f .gitattributes tmp one two three &&
	echo "two text" > .gitattributes &&
	git read-tree --reset -u HEAD &&

	# Note, "normalized" means that git will normalize it if added
	has_cr two &&
	twodiff=`git diff two` &&
	test -n "$twodiff"
'

test_expect_success 'eol=crlf gives a normalized file CRLFs with autocrlf=false' '

	rm -f .gitattributes tmp one two three &&
	git config core.autocrlf false &&
	echo "one eol=crlf" > .gitattributes &&
	git read-tree --reset -u HEAD &&

	has_cr one &&
	onediff=`git diff one` &&
	test -z "$onediff"
'

test_expect_success 'eol=crlf gives a normalized file CRLFs with autocrlf=input' '

	rm -f .gitattributes tmp one two three &&
	git config core.autocrlf input &&
	echo "one eol=crlf" > .gitattributes &&
	git read-tree --reset -u HEAD &&

	has_cr one &&
	onediff=`git diff one` &&
	test -z "$onediff"
'

test_expect_success 'eol=lf gives a normalized file LFs with autocrlf=true' '

	rm -f .gitattributes tmp one two three &&
	git config core.autocrlf true &&
	echo "one eol=lf" > .gitattributes &&
	git read-tree --reset -u HEAD &&

	! has_cr one &&
	onediff=`git diff one` &&
	test -z "$onediff"
'

test_expect_success 'autocrlf=true does not normalize CRLF files' '

	rm -f .gitattributes tmp one two three &&
	git config core.autocrlf true &&
	git read-tree --reset -u HEAD &&

	has_cr one &&
	has_cr two &&
	onediff=`git diff one` &&
	twodiff=`git diff two` &&
	threediff=`git diff three` &&
	test -z "$onediff" -a -z "$twodiff" -a -z "$threediff"
'

test_expect_success 'text=auto, autocrlf=true _does_ normalize CRLF files' '

	rm -f .gitattributes tmp one two three &&
	git config core.autocrlf true &&
	echo "* text=auto" > .gitattributes &&
	git read-tree --reset -u HEAD &&

	has_cr one &&
	has_cr two &&
	onediff=`git diff one` &&
	twodiff=`git diff two` &&
	threediff=`git diff three` &&
	test -z "$onediff" -a -n "$twodiff" -a -z "$threediff"
'

test_expect_success 'text=auto, autocrlf=true does not normalize binary files' '

	rm -f .gitattributes tmp one two three &&
	git config core.autocrlf true &&
	echo "* text=auto" > .gitattributes &&
	git read-tree --reset -u HEAD &&

	! has_cr three &&
	threediff=`git diff three` &&
	test -z "$threediff"
'

test_expect_success 'eol=crlf _does_ normalize binary files' '

	rm -f .gitattributes tmp one two three &&
	echo "three eol=crlf" > .gitattributes &&
	git read-tree --reset -u HEAD &&

	has_cr three &&
	threediff=`git diff three` &&
	test -z "$threediff"
'

test_done
