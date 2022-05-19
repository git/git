#!/bin/sh

test_description='Various filesystem issues'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

auml=$(printf '\303\244')
aumlcdiar=$(printf '\141\314\210')

if test_have_prereq CASE_INSENSITIVE_FS
then
	say "will test on a case insensitive filesystem"
	test_case=test_expect_failure
else
	test_case=test_expect_success
fi

if test_have_prereq UTF8_NFD_TO_NFC
then
	say "will test on a unicode corrupting filesystem"
	test_unicode=test_expect_failure
else
	test_unicode=test_expect_success
fi

test_have_prereq SYMLINKS ||
	say "will test on a filesystem lacking symbolic links"

if test_have_prereq CASE_INSENSITIVE_FS
then
test_expect_success "detection of case insensitive filesystem during repo init" '
	test $(but config --bool core.ignorecase) = true
'
else
test_expect_success "detection of case insensitive filesystem during repo init" '
	{
		test_must_fail but config --bool core.ignorecase >/dev/null ||
			test $(but config --bool core.ignorecase) = false
	}
'
fi

if test_have_prereq SYMLINKS
then
test_expect_success "detection of filesystem w/o symlink support during repo init" '
	{
		test_must_fail but config --bool core.symlinks ||
		test "$(but config --bool core.symlinks)" = true
	}
'
else
test_expect_success "detection of filesystem w/o symlink support during repo init" '
	v=$(but config --bool core.symlinks) &&
	test "$v" = false
'
fi

test_expect_success "setup case tests" '
	but config core.ignorecase true &&
	touch camelcase &&
	but add camelcase &&
	but cummit -m "initial" &&
	but tag initial &&
	but checkout -b topic &&
	but mv camelcase tmp &&
	but mv tmp CamelCase &&
	but cummit -m "rename" &&
	but checkout -f main
'

test_expect_success 'rename (case change)' '
	but mv camelcase CamelCase &&
	but cummit -m "rename"
'

test_expect_success 'merge (case change)' '
	rm -f CamelCase &&
	rm -f camelcase &&
	but reset --hard initial &&
	but merge topic
'

test_expect_success CASE_INSENSITIVE_FS 'add directory (with different case)' '
	but reset --hard initial &&
	mkdir -p dir1/dir2 &&
	echo >dir1/dir2/a &&
	echo >dir1/dir2/b &&
	but add dir1/dir2/a &&
	but add dir1/DIR2/b &&
	but ls-files >actual &&
	cat >expected <<-\EOF &&
		camelcase
		dir1/dir2/a
		dir1/dir2/b
	EOF
	test_cmp expected actual
'

test_expect_failure CASE_INSENSITIVE_FS 'add (with different case)' '
	but reset --hard initial &&
	rm camelcase &&
	echo 1 >CamelCase &&
	but add CamelCase &&
	but ls-files >tmp &&
	camel=$(grep -i camelcase tmp) &&
	test $(echo "$camel" | wc -l) = 1 &&
	test "z$(but cat-file blob :$camel)" = z1
'

test_expect_success "setup unicode normalization tests" '
	test_create_repo unicode &&
	cd unicode &&
	but config core.precomposeunicode false &&
	touch "$aumlcdiar" &&
	but add "$aumlcdiar" &&
	but cummit -m initial &&
	but tag initial &&
	but checkout -b topic &&
	but mv $aumlcdiar tmp &&
	but mv tmp "$auml" &&
	but cummit -m rename &&
	but checkout -f main
'

$test_unicode 'rename (silent unicode normalization)' '
	but mv "$aumlcdiar" "$auml" &&
	but cummit -m rename
'

$test_unicode 'merge (silent unicode normalization)' '
	but reset --hard initial &&
	but merge topic
'

test_expect_success CASE_INSENSITIVE_FS 'checkout with no pathspec and a case insensitive fs' '
	but init repo &&
	(
		cd repo &&

		>Gitweb &&
		but add Gitweb &&
		but cummit -m "add Gitweb" &&

		but checkout --orphan todo &&
		but reset --hard &&
		mkdir -p butweb/subdir &&
		>butweb/subdir/file &&
		but add butweb &&
		but cummit -m "add butweb/subdir/file" &&

		but checkout main
	)
'

test_done
