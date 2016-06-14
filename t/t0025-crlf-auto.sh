#!/bin/sh

test_description='CRLF conversion'

. ./test-lib.sh

has_cr() {
	tr '\015' Q <"$1" | grep Q >/dev/null
}

test_expect_success setup '

	git config core.autocrlf false &&

	for w in Hello world how are you; do echo $w; done >LFonly &&
	for w in I am very very fine thank you; do echo ${w}Q; done | q_to_cr >CRLFonly &&
	for w in Oh here is a QNUL byte how alarming; do echo ${w}; done | q_to_nul >LFwithNUL &&
	git add . &&

	git commit -m initial &&

	LFonly=$(git rev-parse HEAD:LFonly) &&
	CRLFonly=$(git rev-parse HEAD:CRLFonly) &&
	LFwithNUL=$(git rev-parse HEAD:LFwithNUL) &&

	echo happy.
'

test_expect_success 'default settings cause no changes' '

	rm -f .gitattributes tmp LFonly CRLFonly LFwithNUL &&
	git read-tree --reset -u HEAD &&

	! has_cr LFonly &&
	has_cr CRLFonly &&
	LFonlydiff=$(git diff LFonly) &&
	CRLFonlydiff=$(git diff CRLFonly) &&
	LFwithNULdiff=$(git diff LFwithNUL) &&
	test -z "$LFonlydiff" -a -z "$CRLFonlydiff" -a -z "$LFwithNULdiff"
'

test_expect_success 'crlf=true causes a CRLF file to be normalized' '

	# Backwards compatibility check
	rm -f .gitattributes tmp LFonly CRLFonly LFwithNUL &&
	echo "CRLFonly crlf" > .gitattributes &&
	git read-tree --reset -u HEAD &&

	# Note, "normalized" means that git will normalize it if added
	has_cr CRLFonly &&
	CRLFonlydiff=$(git diff CRLFonly) &&
	test -n "$CRLFonlydiff"
'

test_expect_success 'text=true causes a CRLF file to be normalized' '

	rm -f .gitattributes tmp LFonly CRLFonly LFwithNUL &&
	echo "CRLFonly text" > .gitattributes &&
	git read-tree --reset -u HEAD &&

	# Note, "normalized" means that git will normalize it if added
	has_cr CRLFonly &&
	CRLFonlydiff=$(git diff CRLFonly) &&
	test -n "$CRLFonlydiff"
'

test_expect_success 'eol=crlf gives a normalized file CRLFs with autocrlf=false' '

	rm -f .gitattributes tmp LFonly CRLFonly LFwithNUL &&
	git config core.autocrlf false &&
	echo "LFonly eol=crlf" > .gitattributes &&
	git read-tree --reset -u HEAD &&

	has_cr LFonly &&
	LFonlydiff=$(git diff LFonly) &&
	test -z "$LFonlydiff"
'

test_expect_success 'eol=crlf gives a normalized file CRLFs with autocrlf=input' '

	rm -f .gitattributes tmp LFonly CRLFonly LFwithNUL &&
	git config core.autocrlf input &&
	echo "LFonly eol=crlf" > .gitattributes &&
	git read-tree --reset -u HEAD &&

	has_cr LFonly &&
	LFonlydiff=$(git diff LFonly) &&
	test -z "$LFonlydiff"
'

test_expect_success 'eol=lf gives a normalized file LFs with autocrlf=true' '

	rm -f .gitattributes tmp LFonly CRLFonly LFwithNUL &&
	git config core.autocrlf true &&
	echo "LFonly eol=lf" > .gitattributes &&
	git read-tree --reset -u HEAD &&

	! has_cr LFonly &&
	LFonlydiff=$(git diff LFonly) &&
	test -z "$LFonlydiff"
'

test_expect_success 'autocrlf=true does not normalize CRLF files' '

	rm -f .gitattributes tmp LFonly CRLFonly LFwithNUL &&
	git config core.autocrlf true &&
	git read-tree --reset -u HEAD &&

	has_cr LFonly &&
	has_cr CRLFonly &&
	LFonlydiff=$(git diff LFonly) &&
	CRLFonlydiff=$(git diff CRLFonly) &&
	LFwithNULdiff=$(git diff LFwithNUL) &&
	test -z "$LFonlydiff" -a -z "$CRLFonlydiff" -a -z "$LFwithNULdiff"
'

test_expect_success 'text=auto, autocrlf=true does not normalize CRLF files' '

	rm -f .gitattributes tmp LFonly CRLFonly LFwithNUL &&
	git config core.autocrlf true &&
	echo "* text=auto" > .gitattributes &&
	git read-tree --reset -u HEAD &&

	has_cr LFonly &&
	has_cr CRLFonly &&
	LFonlydiff=$(git diff LFonly) &&
	CRLFonlydiff=$(git diff CRLFonly) &&
	LFwithNULdiff=$(git diff LFwithNUL) &&
	test -z "$LFonlydiff" -a -z "$CRLFonlydiff" -a -z "$LFwithNULdiff"
'

test_expect_success 'text=auto, autocrlf=true does not normalize binary files' '

	rm -f .gitattributes tmp LFonly CRLFonly LFwithNUL &&
	git config core.autocrlf true &&
	echo "* text=auto" > .gitattributes &&
	git read-tree --reset -u HEAD &&

	! has_cr LFwithNUL &&
	LFwithNULdiff=$(git diff LFwithNUL) &&
	test -z "$LFwithNULdiff"
'

test_expect_success 'eol=crlf _does_ normalize binary files' '

	rm -f .gitattributes tmp LFonly CRLFonly LFwithNUL &&
	echo "LFwithNUL eol=crlf" > .gitattributes &&
	git read-tree --reset -u HEAD &&

	has_cr LFwithNUL &&
	LFwithNULdiff=$(git diff LFwithNUL) &&
	test -z "$LFwithNULdiff"
'

test_done
