#!/bin/sh

test_description='test case insensitive pathspec limiting'
. ./test-lib.sh

if test_have_prereq CASE_INSENSITIVE_FS
then
	skip_all='skipping case sensitive tests - case insensitive file system'
	test_done
fi

test_expect_success 'create cummits with glob characters' '
	test_cummit bar bar &&
	test_cummit bAr bAr &&
	test_cummit BAR BAR &&
	mkdir foo &&
	test_cummit foo/bar foo/bar &&
	test_cummit foo/bAr foo/bAr &&
	test_cummit foo/BAR foo/BAR &&
	mkdir fOo &&
	test_cummit fOo/bar fOo/bar &&
	test_cummit fOo/bAr fOo/bAr &&
	test_cummit fOo/BAR fOo/BAR &&
	mkdir FOO &&
	test_cummit FOO/bar FOO/bar &&
	test_cummit FOO/bAr FOO/bAr &&
	test_cummit FOO/BAR FOO/BAR
'

test_expect_success 'tree_entry_interesting matches bar' '
	echo bar >expect &&
	git log --format=%s -- "bar" >actual &&
	test_cmp expect actual
'

test_expect_success 'tree_entry_interesting matches :(icase)bar' '
	cat <<-EOF >expect &&
	BAR
	bAr
	bar
	EOF
	git log --format=%s -- ":(icase)bar" >actual &&
	test_cmp expect actual
'

test_expect_success 'tree_entry_interesting matches :(icase)bar with prefix' '
	cat <<-EOF >expect &&
	fOo/BAR
	fOo/bAr
	fOo/bar
	EOF
	( cd fOo && git log --format=%s -- ":(icase)bar" ) >actual &&
	test_cmp expect actual
'

test_expect_success 'tree_entry_interesting matches :(icase)bar with empty prefix' '
	cat <<-EOF >expect &&
	FOO/BAR
	FOO/bAr
	FOO/bar
	fOo/BAR
	fOo/bAr
	fOo/bar
	foo/BAR
	foo/bAr
	foo/bar
	EOF
	( cd fOo && git log --format=%s -- ":(icase)../foo/bar" ) >actual &&
	test_cmp expect actual
'

test_expect_success 'match_pathspec matches :(icase)bar' '
	cat <<-EOF >expect &&
	BAR
	bAr
	bar
	EOF
	git ls-files ":(icase)bar" >actual &&
	test_cmp expect actual
'

test_expect_success 'match_pathspec matches :(icase)bar with prefix' '
	cat <<-EOF >expect &&
	fOo/BAR
	fOo/bAr
	fOo/bar
	EOF
	( cd fOo && git ls-files --full-name ":(icase)bar" ) >actual &&
	test_cmp expect actual
'

test_expect_success 'match_pathspec matches :(icase)bar with empty prefix' '
	cat <<-EOF >expect &&
	bar
	fOo/BAR
	fOo/bAr
	fOo/bar
	EOF
	( cd fOo && git ls-files --full-name ":(icase)bar" ../bar ) >actual &&
	test_cmp expect actual
'

test_expect_success '"git diff" can take magic :(icase) pathspec' '
	echo FOO/BAR >expect &&
	git diff --name-only HEAD^ HEAD -- ":(icase)foo/bar" >actual &&
	test_cmp expect actual
'

test_done
