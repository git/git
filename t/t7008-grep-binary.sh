#!/bin/sh

test_description='git grep in binary files'

. ./test-lib.sh

test_expect_success 'setup' "
	printf 'binary\000file\n' >a &&
	git add a &&
	git commit -m.
"

test_expect_success 'git grep ina a' '
	echo Binary file a matches >expect &&
	git grep ina a >actual &&
	test_cmp expect actual
'

test_expect_success 'git grep -ah ina a' '
	git grep -ah ina a >actual &&
	test_cmp a actual
'

test_expect_success 'git grep -I ina a' '
	: >expect &&
	test_must_fail git grep -I ina a >actual &&
	test_cmp expect actual
'

test_expect_success 'git grep -c ina a' '
	echo a:1 >expect &&
	git grep -c ina a >actual &&
	test_cmp expect actual
'

test_expect_success 'git grep -l ina a' '
	echo a >expect &&
	git grep -l ina a >actual &&
	test_cmp expect actual
'

test_expect_success 'git grep -L bar a' '
	echo a >expect &&
	git grep -L bar a >actual &&
	test_cmp expect actual
'

test_expect_success 'git grep -q ina a' '
	: >expect &&
	git grep -q ina a >actual &&
	test_cmp expect actual
'

test_expect_success 'git grep -F ile a' '
	git grep -F ile a
'

test_expect_success 'git grep -Fi iLE a' '
	git grep -Fi iLE a
'

# This test actually passes on platforms where regexec() supports the
# flag REG_STARTEND.
test_expect_failure 'git grep ile a' '
	git grep ile a
'

test_expect_failure 'git grep .fi a' '
	git grep .fi a
'

test_expect_success 'git grep -F y<NUL>f a' "
	printf 'y\000f' >f &&
	git grep -f f -F a
"

test_expect_success 'git grep -F y<NUL>x a' "
	printf 'y\000x' >f &&
	test_must_fail git grep -f f -F a
"

test_expect_success 'git grep -Fi Y<NUL>f a' "
	printf 'Y\000f' >f &&
	git grep -f f -Fi a
"

test_expect_failure 'git grep -Fi Y<NUL>x a' "
	printf 'Y\000x' >f &&
	test_must_fail git grep -f f -Fi a
"

test_expect_success 'git grep y<NUL>f a' "
	printf 'y\000f' >f &&
	git grep -f f a
"

test_expect_failure 'git grep y<NUL>x a' "
	printf 'y\000x' >f &&
	test_must_fail git grep -f f a
"

test_done
