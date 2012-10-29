#!/bin/sh

test_description='git grep in binary files'

. ./test-lib.sh

test_expect_success 'setup' "
	echo 'binaryQfile' | q_to_nul >a &&
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
test_expect_success 'git grep ile a' '
	git grep ile a
'

test_expect_failure 'git grep .fi a' '
	git grep .fi a
'

test_expect_success 'git grep -F y<NUL>f a' "
	printf 'yQf' | q_to_nul >f &&
	git grep -f f -F a
"

test_expect_success 'git grep -F y<NUL>x a' "
	printf 'yQx' | q_to_nul >f &&
	test_must_fail git grep -f f -F a
"

test_expect_success 'git grep -Fi Y<NUL>f a' "
	printf 'YQf' | q_to_nul >f &&
	git grep -f f -Fi a
"

test_expect_success 'git grep -Fi Y<NUL>x a' "
	printf 'YQx' | q_to_nul >f &&
	test_must_fail git grep -f f -Fi a
"

test_expect_success 'git grep y<NUL>f a' "
	printf 'yQf' | q_to_nul >f &&
	git grep -f f a
"

test_expect_success 'git grep y<NUL>x a' "
	printf 'yQx' | q_to_nul >f &&
	test_must_fail git grep -f f a
"

test_expect_success 'grep respects binary diff attribute' '
	echo text >t &&
	git add t &&
	echo t:text >expect &&
	git grep text t >actual &&
	test_cmp expect actual &&
	echo "t -diff" >.gitattributes &&
	echo "Binary file t matches" >expect &&
	git grep text t >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --cached respects binary diff attribute' '
	git grep --cached text t >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --cached respects binary diff attribute (2)' '
	git add .gitattributes &&
	rm .gitattributes &&
	git grep --cached text t >actual &&
	test_when_finished "git rm --cached .gitattributes" &&
	test_when_finished "git checkout .gitattributes" &&
	test_cmp expect actual
'

test_expect_success 'grep revision respects binary diff attribute' '
	git commit -m new &&
	echo "Binary file HEAD:t matches" >expect &&
	git grep text HEAD -- t >actual &&
	test_when_finished "git reset HEAD^" &&
	test_cmp expect actual
'

test_expect_success 'grep respects not-binary diff attribute' '
	echo binQary | q_to_nul >b &&
	git add b &&
	echo "Binary file b matches" >expect &&
	git grep bin b >actual &&
	test_cmp expect actual &&
	echo "b diff" >.gitattributes &&
	echo "b:binQary" >expect &&
	git grep bin b | nul_to_q >actual &&
	test_cmp expect actual
'

test_done
