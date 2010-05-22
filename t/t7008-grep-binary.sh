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

test_done
