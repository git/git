#!/bin/sh

test_description='CRLF conversion'

. ./test-lib.sh

has_cr() {
	tr '\015' Q <"$1" | grep Q >/dev/null
}

# add or remove CRs to disk file in-place
# usage: munge_cr <append|remove> <file>
munge_cr () {
	"${1}_cr" <"$2" >tmp &&
	mv tmp "$2"
}

test_expect_success setup '

	git config core.autocrlf false &&

	for w in Hello world how are you; do echo $w; done >one &&
	mkdir dir &&
	for w in I am very very fine thank you; do echo $w; done >dir/two &&
	for w in Oh here is NULQin text here; do echo $w; done | q_to_nul >three &&
	git add . &&

	git commit -m initial &&

	one=$(git rev-parse HEAD:one) &&
	dir=$(git rev-parse HEAD:dir) &&
	two=$(git rev-parse HEAD:dir/two) &&
	three=$(git rev-parse HEAD:three) &&

	for w in Some extra lines here; do echo $w; done >>one &&
	git diff >patch.file &&
	patched=$(git hash-object --stdin <one) &&
	git read-tree --reset -u HEAD
'

test_expect_success 'safecrlf: autocrlf=input, all CRLF' '

	git config core.autocrlf input &&
	git config core.safecrlf true &&

	for w in I am all CRLF; do echo $w; done | append_cr >allcrlf &&
	test_must_fail git add allcrlf
'

test_expect_success 'safecrlf: autocrlf=input, mixed LF/CRLF' '

	git config core.autocrlf input &&
	git config core.safecrlf true &&

	for w in Oh here is CRLFQ in text; do echo $w; done | q_to_cr >mixed &&
	test_must_fail git add mixed
'

test_expect_success 'safecrlf: autocrlf=true, all LF' '

	git config core.autocrlf true &&
	git config core.safecrlf true &&

	for w in I am all LF; do echo $w; done >alllf &&
	test_must_fail git add alllf
'

test_expect_success 'safecrlf: autocrlf=true mixed LF/CRLF' '

	git config core.autocrlf true &&
	git config core.safecrlf true &&

	for w in Oh here is CRLFQ in text; do echo $w; done | q_to_cr >mixed &&
	test_must_fail git add mixed
'

test_expect_success 'safecrlf: print warning only once' '

	git config core.autocrlf input &&
	git config core.safecrlf warn &&

	for w in I am all LF; do echo $w; done >doublewarn &&
	git add doublewarn &&
	git commit -m "nowarn" &&
	for w in Oh here is CRLFQ in text; do echo $w; done | q_to_cr >doublewarn &&
	git add doublewarn 2>err &&
	if test_have_prereq C_LOCALE_OUTPUT
	then
		test $(grep "CRLF will be replaced by LF" err | wc -l) = 1
	fi
'


test_expect_success 'safecrlf: git diff demotes safecrlf=true to warn' '
	git config core.autocrlf input &&
	git config core.safecrlf true &&
	git diff HEAD
'


test_expect_success 'switch off autocrlf, safecrlf, reset HEAD' '
	git config core.autocrlf false &&
	git config core.safecrlf false &&
	git reset --hard HEAD^
'

test_expect_success 'update with autocrlf=input' '

	rm -f tmp one dir/two three &&
	git read-tree --reset -u HEAD &&
	git config core.autocrlf input &&
	munge_cr append one &&
	munge_cr append dir/two &&
	git update-index -- one dir/two &&
	differs=$(git diff-index --cached HEAD) &&
	verbose test -z "$differs"

'

test_expect_success 'update with autocrlf=true' '

	rm -f tmp one dir/two three &&
	git read-tree --reset -u HEAD &&
	git config core.autocrlf true &&
	munge_cr append one &&
	munge_cr append dir/two &&
	git update-index -- one dir/two &&
	differs=$(git diff-index --cached HEAD) &&
	verbose test -z "$differs"

'

test_expect_success 'checkout with autocrlf=true' '

	rm -f tmp one dir/two three &&
	git config core.autocrlf true &&
	git read-tree --reset -u HEAD &&
	munge_cr remove one &&
	munge_cr remove dir/two &&
	git update-index -- one dir/two &&
	test "$one" = $(git hash-object --stdin <one) &&
	test "$two" = $(git hash-object --stdin <dir/two) &&
	differs=$(git diff-index --cached HEAD) &&
	verbose test -z "$differs"
'

test_expect_success 'checkout with autocrlf=input' '

	rm -f tmp one dir/two three &&
	git config core.autocrlf input &&
	git read-tree --reset -u HEAD &&
	test_must_fail has_cr one &&
	test_must_fail has_cr two &&
	git update-index -- one dir/two &&
	test "$one" = $(git hash-object --stdin <one) &&
	test "$two" = $(git hash-object --stdin <dir/two) &&
	differs=$(git diff-index --cached HEAD) &&
	verbose test -z "$differs"
'

test_expect_success 'apply patch (autocrlf=input)' '

	rm -f tmp one dir/two three &&
	git config core.autocrlf input &&
	git read-tree --reset -u HEAD &&

	git apply patch.file &&
	verbose test "$patched" = "$(git hash-object --stdin <one)"
'

test_expect_success 'apply patch --cached (autocrlf=input)' '

	rm -f tmp one dir/two three &&
	git config core.autocrlf input &&
	git read-tree --reset -u HEAD &&

	git apply --cached patch.file &&
	verbose test "$patched" = $(git rev-parse :one)
'

test_expect_success 'apply patch --index (autocrlf=input)' '

	rm -f tmp one dir/two three &&
	git config core.autocrlf input &&
	git read-tree --reset -u HEAD &&

	git apply --index patch.file &&
	verbose test "$patched" = $(git rev-parse :one) &&
	verbose test "$patched" = $(git hash-object --stdin <one)
'

test_expect_success 'apply patch (autocrlf=true)' '

	rm -f tmp one dir/two three &&
	git config core.autocrlf true &&
	git read-tree --reset -u HEAD &&

	git apply patch.file &&
	verbose test "$patched" = "$(remove_cr <one | git hash-object --stdin)"
'

test_expect_success 'apply patch --cached (autocrlf=true)' '

	rm -f tmp one dir/two three &&
	git config core.autocrlf true &&
	git read-tree --reset -u HEAD &&

	git apply --cached patch.file &&
	verbose test "$patched" = $(git rev-parse :one)
'

test_expect_success 'apply patch --index (autocrlf=true)' '

	rm -f tmp one dir/two three &&
	git config core.autocrlf true &&
	git read-tree --reset -u HEAD &&

	git apply --index patch.file &&
	verbose test "$patched" = $(git rev-parse :one) &&
	verbose test "$patched" = "$(remove_cr <one | git hash-object --stdin)"
'

test_expect_success '.gitattributes says two is binary' '

	rm -f tmp one dir/two three &&
	echo "two -crlf" >.gitattributes &&
	git config core.autocrlf true &&
	git read-tree --reset -u HEAD &&

	test_must_fail has_cr dir/two &&
	verbose has_cr one &&
	test_must_fail has_cr three
'

test_expect_success '.gitattributes says two is input' '

	rm -f tmp one dir/two three &&
	echo "two crlf=input" >.gitattributes &&
	git read-tree --reset -u HEAD &&

	test_must_fail has_cr dir/two
'

test_expect_success '.gitattributes says two and three are text' '

	rm -f tmp one dir/two three &&
	echo "t* crlf" >.gitattributes &&
	git read-tree --reset -u HEAD &&

	verbose has_cr dir/two &&
	verbose has_cr three
'

test_expect_success 'in-tree .gitattributes (1)' '

	echo "one -crlf" >>.gitattributes &&
	git add .gitattributes &&
	git commit -m "Add .gitattributes" &&

	rm -rf tmp one dir .gitattributes patch.file three &&
	git read-tree --reset -u HEAD &&

	test_must_fail has_cr one &&
	verbose has_cr three
'

test_expect_success 'in-tree .gitattributes (2)' '

	rm -rf tmp one dir .gitattributes patch.file three &&
	git read-tree --reset HEAD &&
	git checkout-index -f -q -u -a &&

	test_must_fail has_cr one &&
	verbose has_cr three
'

test_expect_success 'in-tree .gitattributes (3)' '

	rm -rf tmp one dir .gitattributes patch.file three &&
	git read-tree --reset HEAD &&
	git checkout-index -u .gitattributes &&
	git checkout-index -u one dir/two three &&

	test_must_fail has_cr one &&
	verbose has_cr three
'

test_expect_success 'in-tree .gitattributes (4)' '

	rm -rf tmp one dir .gitattributes patch.file three &&
	git read-tree --reset HEAD &&
	git checkout-index -u one dir/two three &&
	git checkout-index -u .gitattributes &&

	test_must_fail has_cr one &&
	verbose has_cr three
'

test_expect_success 'checkout with existing .gitattributes' '

	git config core.autocrlf true &&
	git config --unset core.safecrlf &&
	echo ".file2 -crlfQ" | q_to_cr >> .gitattributes &&
	git add .gitattributes &&
	git commit -m initial &&
	echo ".file -crlfQ" | q_to_cr >> .gitattributes &&
	echo "contents" > .file &&
	git add .gitattributes .file &&
	git commit -m second &&

	git checkout master~1 &&
	git checkout master &&
	test "$(git diff-files --raw)" = ""

'

test_expect_success 'checkout when deleting .gitattributes' '

	git rm .gitattributes &&
	echo "contentsQ" | q_to_cr > .file2 &&
	git add .file2 &&
	git commit -m third &&

	git checkout master~1 &&
	git checkout master &&
	has_cr .file2

'

test_expect_success 'invalid .gitattributes (must not crash)' '

	echo "three +crlf" >>.gitattributes &&
	git diff

'
# Some more tests here to add new autocrlf functionality.
# We want to have a known state here, so start a bit from scratch

test_expect_success 'setting up for new autocrlf tests' '
	git config core.autocrlf false &&
	git config core.safecrlf false &&
	rm -rf .????* * &&
	for w in I am all LF; do echo $w; done >alllf &&
	for w in Oh here is CRLFQ in text; do echo $w; done | q_to_cr >mixed &&
	for w in I am all CRLF; do echo $w; done | append_cr >allcrlf &&
	git add -A . &&
	git commit -m "alllf, allcrlf and mixed only" &&
	git tag -a -m "message" autocrlf-checkpoint
'

test_expect_success 'report no change after setting autocrlf' '
	git config core.autocrlf true &&
	touch * &&
	git diff --exit-code
'

test_expect_success 'files are clean after checkout' '
	rm * &&
	git checkout -f &&
	git diff --exit-code
'

cr_to_Q_no_NL () {
    tr '\015' Q | tr -d '\012'
}

test_expect_success 'LF only file gets CRLF with autocrlf' '
	test "$(cr_to_Q_no_NL < alllf)" = "IQamQallQLFQ"
'

test_expect_success 'Mixed file is still mixed with autocrlf' '
	test "$(cr_to_Q_no_NL < mixed)" = "OhhereisCRLFQintext"
'

test_expect_success 'CRLF only file has CRLF with autocrlf' '
	test "$(cr_to_Q_no_NL < allcrlf)" = "IQamQallQCRLFQ"
'

test_expect_success 'New CRLF file gets LF in repo' '
	tr -d "\015" < alllf | append_cr > alllf2 &&
	git add alllf2 &&
	git commit -m "alllf2 added" &&
	git config core.autocrlf false &&
	rm * &&
	git checkout -f &&
	test_cmp alllf alllf2
'

test_done
