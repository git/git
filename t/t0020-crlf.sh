#!/bin/sh

test_description='CRLF conversion'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
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

	but config core.autocrlf false &&

	test_write_lines Hello world how are you >one &&
	mkdir dir &&
	test_write_lines I am very very fine thank you >dir/two &&
	test_write_lines Oh here is NULQin text here | q_to_nul >three &&
	but add . &&

	but cummit -m initial &&

	one=$(but rev-parse HEAD:one) &&
	dir=$(but rev-parse HEAD:dir) &&
	two=$(but rev-parse HEAD:dir/two) &&
	three=$(but rev-parse HEAD:three) &&

	test_write_lines Some extra lines here >>one &&
	but diff >patch.file &&
	patched=$(but hash-object --stdin <one) &&
	but read-tree --reset -u HEAD
'

test_expect_success 'safecrlf: autocrlf=input, all CRLF' '

	but config core.autocrlf input &&
	but config core.safecrlf true &&

	test_write_lines I am all CRLF | append_cr >allcrlf &&
	test_must_fail but add allcrlf
'

test_expect_success 'safecrlf: autocrlf=input, mixed LF/CRLF' '

	but config core.autocrlf input &&
	but config core.safecrlf true &&

	test_write_lines Oh here is CRLFQ in text | q_to_cr >mixed &&
	test_must_fail but add mixed
'

test_expect_success 'safecrlf: autocrlf=true, all LF' '

	but config core.autocrlf true &&
	but config core.safecrlf true &&

	test_write_lines I am all LF >alllf &&
	test_must_fail but add alllf
'

test_expect_success 'safecrlf: autocrlf=true mixed LF/CRLF' '

	but config core.autocrlf true &&
	but config core.safecrlf true &&

	test_write_lines Oh here is CRLFQ in text | q_to_cr >mixed &&
	test_must_fail but add mixed
'

test_expect_success 'safecrlf: print warning only once' '

	but config core.autocrlf input &&
	but config core.safecrlf warn &&

	test_write_lines I am all LF >doublewarn &&
	but add doublewarn &&
	but cummit -m "nowarn" &&
	test_write_lines Oh here is CRLFQ in text | q_to_cr >doublewarn &&
	but add doublewarn 2>err &&
	grep "CRLF will be replaced by LF" err >err.warnings &&
	test_line_count = 1 err.warnings
'


test_expect_success 'safecrlf: but diff demotes safecrlf=true to warn' '
	but config core.autocrlf input &&
	but config core.safecrlf true &&
	but diff HEAD
'


test_expect_success 'safecrlf: no warning with safecrlf=false' '
	but config core.autocrlf input &&
	but config core.safecrlf false &&

	test_write_lines I am all CRLF | append_cr >allcrlf &&
	but add allcrlf 2>err &&
	test_must_be_empty err
'


test_expect_success 'switch off autocrlf, safecrlf, reset HEAD' '
	but config core.autocrlf false &&
	but config core.safecrlf false &&
	but reset --hard HEAD^
'

test_expect_success 'update with autocrlf=input' '

	rm -f tmp one dir/two three &&
	but read-tree --reset -u HEAD &&
	but config core.autocrlf input &&
	munge_cr append one &&
	munge_cr append dir/two &&
	but update-index -- one dir/two &&
	differs=$(but diff-index --cached HEAD) &&
	verbose test -z "$differs"

'

test_expect_success 'update with autocrlf=true' '

	rm -f tmp one dir/two three &&
	but read-tree --reset -u HEAD &&
	but config core.autocrlf true &&
	munge_cr append one &&
	munge_cr append dir/two &&
	but update-index -- one dir/two &&
	differs=$(but diff-index --cached HEAD) &&
	verbose test -z "$differs"

'

test_expect_success 'checkout with autocrlf=true' '

	rm -f tmp one dir/two three &&
	but config core.autocrlf true &&
	but read-tree --reset -u HEAD &&
	munge_cr remove one &&
	munge_cr remove dir/two &&
	but update-index -- one dir/two &&
	test "$one" = $(but hash-object --stdin <one) &&
	test "$two" = $(but hash-object --stdin <dir/two) &&
	differs=$(but diff-index --cached HEAD) &&
	verbose test -z "$differs"
'

test_expect_success 'checkout with autocrlf=input' '

	rm -f tmp one dir/two three &&
	but config core.autocrlf input &&
	but read-tree --reset -u HEAD &&
	! has_cr one &&
	! has_cr dir/two &&
	but update-index -- one dir/two &&
	test "$one" = $(but hash-object --stdin <one) &&
	test "$two" = $(but hash-object --stdin <dir/two) &&
	differs=$(but diff-index --cached HEAD) &&
	verbose test -z "$differs"
'

test_expect_success 'apply patch (autocrlf=input)' '

	rm -f tmp one dir/two three &&
	but config core.autocrlf input &&
	but read-tree --reset -u HEAD &&

	but apply patch.file &&
	verbose test "$patched" = "$(but hash-object --stdin <one)"
'

test_expect_success 'apply patch --cached (autocrlf=input)' '

	rm -f tmp one dir/two three &&
	but config core.autocrlf input &&
	but read-tree --reset -u HEAD &&

	but apply --cached patch.file &&
	verbose test "$patched" = $(but rev-parse :one)
'

test_expect_success 'apply patch --index (autocrlf=input)' '

	rm -f tmp one dir/two three &&
	but config core.autocrlf input &&
	but read-tree --reset -u HEAD &&

	but apply --index patch.file &&
	verbose test "$patched" = $(but rev-parse :one) &&
	verbose test "$patched" = $(but hash-object --stdin <one)
'

test_expect_success 'apply patch (autocrlf=true)' '

	rm -f tmp one dir/two three &&
	but config core.autocrlf true &&
	but read-tree --reset -u HEAD &&

	but apply patch.file &&
	verbose test "$patched" = "$(remove_cr <one | but hash-object --stdin)"
'

test_expect_success 'apply patch --cached (autocrlf=true)' '

	rm -f tmp one dir/two three &&
	but config core.autocrlf true &&
	but read-tree --reset -u HEAD &&

	but apply --cached patch.file &&
	verbose test "$patched" = $(but rev-parse :one)
'

test_expect_success 'apply patch --index (autocrlf=true)' '

	rm -f tmp one dir/two three &&
	but config core.autocrlf true &&
	but read-tree --reset -u HEAD &&

	but apply --index patch.file &&
	verbose test "$patched" = $(but rev-parse :one) &&
	verbose test "$patched" = "$(remove_cr <one | but hash-object --stdin)"
'

test_expect_success '.butattributes says two is binary' '

	rm -f tmp one dir/two three &&
	echo "two -crlf" >.butattributes &&
	but config core.autocrlf true &&
	but read-tree --reset -u HEAD &&

	! has_cr dir/two &&
	verbose has_cr one &&
	! has_cr three
'

test_expect_success '.butattributes says two is input' '

	rm -f tmp one dir/two three &&
	echo "two crlf=input" >.butattributes &&
	but read-tree --reset -u HEAD &&

	! has_cr dir/two
'

test_expect_success '.butattributes says two and three are text' '

	rm -f tmp one dir/two three &&
	echo "t* crlf" >.butattributes &&
	but read-tree --reset -u HEAD &&

	verbose has_cr dir/two &&
	verbose has_cr three
'

test_expect_success 'in-tree .butattributes (1)' '

	echo "one -crlf" >>.butattributes &&
	but add .butattributes &&
	but cummit -m "Add .butattributes" &&

	rm -rf tmp one dir .butattributes patch.file three &&
	but read-tree --reset -u HEAD &&

	! has_cr one &&
	verbose has_cr three
'

test_expect_success 'in-tree .butattributes (2)' '

	rm -rf tmp one dir .butattributes patch.file three &&
	but read-tree --reset HEAD &&
	but checkout-index -f -q -u -a &&

	! has_cr one &&
	verbose has_cr three
'

test_expect_success 'in-tree .butattributes (3)' '

	rm -rf tmp one dir .butattributes patch.file three &&
	but read-tree --reset HEAD &&
	but checkout-index -u .butattributes &&
	but checkout-index -u one dir/two three &&

	! has_cr one &&
	verbose has_cr three
'

test_expect_success 'in-tree .butattributes (4)' '

	rm -rf tmp one dir .butattributes patch.file three &&
	but read-tree --reset HEAD &&
	but checkout-index -u one dir/two three &&
	but checkout-index -u .butattributes &&

	! has_cr one &&
	verbose has_cr three
'

test_expect_success 'checkout with existing .butattributes' '

	but config core.autocrlf true &&
	but config --unset core.safecrlf &&
	echo ".file2 -crlfQ" | q_to_cr >> .butattributes &&
	but add .butattributes &&
	but cummit -m initial &&
	echo ".file -crlfQ" | q_to_cr >> .butattributes &&
	echo "contents" > .file &&
	but add .butattributes .file &&
	but cummit -m second &&

	but checkout main~1 &&
	but checkout main &&
	test "$(but diff-files --raw)" = ""

'

test_expect_success 'checkout when deleting .butattributes' '

	but rm .butattributes &&
	echo "contentsQ" | q_to_cr > .file2 &&
	but add .file2 &&
	but cummit -m third &&

	but checkout main~1 &&
	but checkout main &&
	has_cr .file2

'

test_expect_success 'invalid .butattributes (must not crash)' '

	echo "three +crlf" >>.butattributes &&
	but diff

'
# Some more tests here to add new autocrlf functionality.
# We want to have a known state here, so start a bit from scratch

test_expect_success 'setting up for new autocrlf tests' '
	but config core.autocrlf false &&
	but config core.safecrlf false &&
	rm -rf .????* * &&
	test_write_lines I am all LF >alllf &&
	test_write_lines Oh here is CRLFQ in text | q_to_cr >mixed &&
	test_write_lines I am all CRLF | append_cr >allcrlf &&
	but add -A . &&
	but cummit -m "alllf, allcrlf and mixed only" &&
	but tag -a -m "message" autocrlf-checkpoint
'

test_expect_success 'report no change after setting autocrlf' '
	but config core.autocrlf true &&
	touch * &&
	but diff --exit-code
'

test_expect_success 'files are clean after checkout' '
	rm * &&
	but checkout -f &&
	but diff --exit-code
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
	but add alllf2 &&
	but cummit -m "alllf2 added" &&
	but config core.autocrlf false &&
	rm * &&
	but checkout -f &&
	test_cmp alllf alllf2
'

test_done
