#!/bin/sh

test_description='git apply with too-large patch'

. ./test-lib.sh

test_expect_success EXPENSIVE 'git apply rejects patches that are too large' '
	{
		cat <<-\EOF &&
		diff --git a/file b/file
		new file mode 100644
		--- /dev/null
		+++ b/file
		@@ -0,0 +1 @@
		EOF
		test-tool genzeros $((1024 * 1024 * 1023))
	} | test_must_fail git apply 2>err &&
	test_grep "patch too large" err
'

test_done
