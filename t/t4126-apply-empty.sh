#!/bin/sh

test_description='apply empty'

. ./test-lib.sh

test_expect_success setup '
	>empty &&
	git add empty &&
	test_tick &&
	git commit -m initial &&
	for i in a b c d e
	do
		echo $i
	done >empty &&
	cat empty >expect &&
	git diff |
	sed -e "/^diff --git/d" \
	    -e "/^index /d" \
	    -e "s|a/empty|empty.orig|" \
	    -e "s|b/empty|empty|" >patch0 &&
	sed -e "s|empty|missing|" patch0 >patch1 &&
	>empty &&
	git update-index --refresh
'

test_expect_success 'apply empty' '
	git reset --hard &&
	rm -f missing &&
	git apply patch0 &&
	test_cmp expect empty
'

test_expect_success 'apply --index empty' '
	git reset --hard &&
	rm -f missing &&
	git apply --index patch0 &&
	test_cmp expect empty &&
	git diff --exit-code
'

test_expect_success 'apply create' '
	git reset --hard &&
	rm -f missing &&
	git apply patch1 &&
	test_cmp expect missing
'

test_expect_success 'apply --index create' '
	git reset --hard &&
	rm -f missing &&
	git apply --index patch1 &&
	test_cmp expect missing &&
	git diff --exit-code
'

test_done
