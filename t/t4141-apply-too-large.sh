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

test_expect_success EXPENSIVE 'git apply rejects binary patches that are too large' '
	sz=$((1024 * 1024 * 2048)) &&
	test-tool genzeros $sz | test-tool zlib deflate | test-tool base85 chunk-encode >payload &&
	{
		# Although the patch requires the correct blob ID, blob ID validation
		# happens after the inflated content size check, so use $EMPTY_BLOB as
		# a filler.
		cat <<-EOF &&
		diff --git a/file b/file
		new file mode 100644
		index $ZERO_OID..$EMPTY_BLOB
		GIT binary patch
		literal $sz
		EOF
		cat payload
	} | test_must_fail git apply 2>err &&
	test_grep "patch too large" err
'

test_expect_success EXPENSIVE 'git apply rejects large binary patches with incorrect size' '
	sz=$((1024 * 1024 * 1023)) &&
	test-tool genrandom foo $sz | test-tool zlib deflate | test-tool base85 chunk-encode >payload &&
	{
		cat <<-EOF &&
		diff --git a/file b/file
		new file mode 100644
		index $ZERO_OID..$EMPTY_BLOB
		GIT binary patch
		literal 10
		EOF
		cat payload
	} | test_must_fail git apply 2>err &&
	test_grep "patch too large" err
'

test_expect_success 'git apply rejects binary patches with incorrect size' '
	test-tool genrandom foo 100 | test-tool zlib deflate | test-tool base85 chunk-encode >payload &&
	{
		cat <<-EOF &&
		diff --git a/file b/file
		new file mode 100644
		index $ZERO_OID..$EMPTY_BLOB
		GIT binary patch
		literal 10
		EOF
		cat payload
	} | test_must_fail git apply 2>err &&
	test_grep "corrupt binary patch" err
'

test_done
