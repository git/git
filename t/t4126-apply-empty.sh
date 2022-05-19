#!/bin/sh

test_description='apply empty'


TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	>empty &&
	but add empty &&
	test_tick &&
	but cummit -m initial &&
	but cummit --allow-empty -m "empty cummit" &&
	but format-patch --always HEAD~ >empty.patch &&
	test_write_lines a b c d e >empty &&
	cat empty >expect &&
	but diff |
	sed -e "/^diff --but/d" \
	    -e "/^index /d" \
	    -e "s|a/empty|empty.orig|" \
	    -e "s|b/empty|empty|" >patch0 &&
	sed -e "s|empty|missing|" patch0 >patch1 &&
	>empty &&
	but update-index --refresh
'

test_expect_success 'apply empty' '
	rm -f missing &&
	test_when_finished "but reset --hard" &&
	but apply patch0 &&
	test_cmp expect empty
'

test_expect_success 'apply empty patch fails' '
	test_when_finished "but reset --hard" &&
	test_must_fail but apply empty.patch &&
	test_must_fail but apply - </dev/null
'

test_expect_success 'apply with --allow-empty succeeds' '
	test_when_finished "but reset --hard" &&
	but apply --allow-empty empty.patch &&
	but apply --allow-empty - </dev/null
'

test_expect_success 'apply --index empty' '
	rm -f missing &&
	test_when_finished "but reset --hard" &&
	but apply --index patch0 &&
	test_cmp expect empty &&
	but diff --exit-code
'

test_expect_success 'apply create' '
	rm -f missing &&
	test_when_finished "but reset --hard" &&
	but apply patch1 &&
	test_cmp expect missing
'

test_expect_success 'apply --index create' '
	rm -f missing &&
	test_when_finished "but reset --hard" &&
	but apply --index patch1 &&
	test_cmp expect missing &&
	but diff --exit-code
'

test_done
