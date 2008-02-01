#!/bin/sh
#
# Copyright (c) 2007 Nguyễn Thái Ngọc Duy
#

test_description='Test repository version check'

. ./test-lib.sh

cat >test.patch <<EOF
diff --git a/test.txt b/test.txt
new file mode 100644
--- /dev/null
+++ b/test.txt
@@ -0,0 +1 @@
+123
EOF

test_create_repo "test"
test_create_repo "test2"

GIT_CONFIG=test2/.git/config git config core.repositoryformatversion 99 || exit 1

test_expect_success 'gitdir selection on normal repos' '
	(test "$(git config core.repositoryformatversion)" = 0 &&
	cd test &&
	test "$(git config core.repositoryformatversion)" = 0)'

# Make sure it would stop at test2, not trash
test_expect_success 'gitdir selection on unsupported repo' '
	(cd test2 &&
	test "$(git config core.repositoryformatversion)" = 99)'

test_expect_success 'gitdir not required mode' '
	(git apply --stat test.patch &&
	cd test && git apply --stat ../test.patch &&
	cd ../test2 && git apply --stat ../test.patch)'

test_expect_success 'gitdir required mode on normal repos' '
	(git apply --check --index test.patch &&
	cd test && git apply --check --index ../test.patch)'

test_expect_success 'gitdir required mode on unsupported repo' '
	(cd test2 && ! git apply --check --index ../test.patch)
'

test_done
