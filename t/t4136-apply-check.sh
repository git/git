#!/bin/sh

test_description='git apply should exit non-zero with unrecognized input.'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit 1
'

test_expect_success 'apply --check exits non-zero with unrecognized input' '
	test_must_fail git apply --check - <<-\EOF
	I am not a patch
	I look nothing like a patch
	git apply must fail
	EOF
'

test_expect_success 'apply exits non-zero with no-op patch' '
	cat >input <<-\EOF &&
	diff --get a/1 b/1
	index 6696ea4..606eddd 100644
	--- a/1
	+++ b/1
	@@ -1,1 +1,1 @@
	 1
	EOF
	test_must_fail git apply --stat input &&
	test_must_fail git apply --check input
'

test_done
