#!/bin/sh

test_description='split index mode tests'

. ./test-lib.sh

# We need total control of index splitting here
sane_unset GIT_TEST_SPLIT_INDEX

test_expect_success 'enable split index' '
	git update-index --split-index &&
	test-dump-split-index .git/index >actual &&
	indexversion=$(test-index-version <.git/index) &&
	if test "$indexversion" = "4"
	then
		own=432ef4b63f32193984f339431fd50ca796493569
		base=508851a7f0dfa8691e9f69c7f055865389012491
	else
		own=8299b0bcd1ac364e5f1d7768efb62fa2da79a339
		base=39d890139ee5356c7ef572216cebcd27aa41f9df
	fi &&
	cat >expect <<EOF &&
own $own
base $base
replacements:
deletions:
EOF
	test_cmp expect actual
'

test_expect_success 'add one file' '
	: >one &&
	git update-index --add one &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<EOF &&
100644 $EMPTY_BLOB 0	one
EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<EOF &&
base $base
100644 $EMPTY_BLOB 0	one
replacements:
deletions:
EOF
	test_cmp expect actual
'

test_expect_success 'disable split index' '
	git update-index --no-split-index &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<EOF &&
100644 $EMPTY_BLOB 0	one
EOF
	test_cmp ls-files.expect ls-files.actual &&

	BASE=$(test-dump-split-index .git/index | grep "^own" | sed "s/own/base/") &&
	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<EOF &&
not a split index
EOF
	test_cmp expect actual
'

test_expect_success 'enable split index again, "one" now belongs to base index"' '
	git update-index --split-index &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<EOF &&
100644 $EMPTY_BLOB 0	one
EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<EOF &&
$BASE
replacements:
deletions:
EOF
	test_cmp expect actual
'

test_expect_success 'modify original file, base index untouched' '
	echo modified >one &&
	git update-index one &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<EOF &&
100644 2e0996000b7e9019eabcad29391bf0f5c7702f0b 0	one
EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	q_to_tab >expect <<EOF &&
$BASE
100644 2e0996000b7e9019eabcad29391bf0f5c7702f0b 0Q
replacements: 0
deletions:
EOF
	test_cmp expect actual
'

test_expect_success 'add another file, which stays index' '
	: >two &&
	git update-index --add two &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<EOF &&
100644 2e0996000b7e9019eabcad29391bf0f5c7702f0b 0	one
100644 $EMPTY_BLOB 0	two
EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	q_to_tab >expect <<EOF &&
$BASE
100644 2e0996000b7e9019eabcad29391bf0f5c7702f0b 0Q
100644 $EMPTY_BLOB 0	two
replacements: 0
deletions:
EOF
	test_cmp expect actual
'

test_expect_success 'remove file not in base index' '
	git update-index --force-remove two &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<EOF &&
100644 2e0996000b7e9019eabcad29391bf0f5c7702f0b 0	one
EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	q_to_tab >expect <<EOF &&
$BASE
100644 2e0996000b7e9019eabcad29391bf0f5c7702f0b 0Q
replacements: 0
deletions:
EOF
	test_cmp expect actual
'

test_expect_success 'remove file in base index' '
	git update-index --force-remove one &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<EOF &&
EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<EOF &&
$BASE
replacements:
deletions: 0
EOF
	test_cmp expect actual
'

test_expect_success 'add original file back' '
	: >one &&
	git update-index --add one &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<EOF &&
100644 $EMPTY_BLOB 0	one
EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<EOF &&
$BASE
100644 $EMPTY_BLOB 0	one
replacements:
deletions: 0
EOF
	test_cmp expect actual
'

test_expect_success 'add new file' '
	: >two &&
	git update-index --add two &&
	git ls-files --stage >actual &&
	cat >expect <<EOF &&
100644 $EMPTY_BLOB 0	one
100644 $EMPTY_BLOB 0	two
EOF
	test_cmp expect actual
'

test_expect_success 'unify index, two files remain' '
	git update-index --no-split-index &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<EOF &&
100644 $EMPTY_BLOB 0	one
100644 $EMPTY_BLOB 0	two
EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<EOF &&
not a split index
EOF
	test_cmp expect actual
'

test_done
