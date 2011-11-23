#!/bin/sh
#
# Copyright (c) 2007 Nguyễn Thái Ngọc Duy
#

test_description='Test repository version check'

. ./test-lib.sh

test_expect_success 'setup' '
	cat >test.patch <<-\EOF &&
	diff --git a/test.txt b/test.txt
	new file mode 100644
	--- /dev/null
	+++ b/test.txt
	@@ -0,0 +1 @@
	+123
	EOF

	test_create_repo "test" &&
	test_create_repo "test2" &&
	GIT_CONFIG=test2/.git/config git config core.repositoryformatversion 99
'

test_expect_success 'gitdir selection on normal repos' '
	echo 0 >expect &&
	git config core.repositoryformatversion >actual &&
	(
		cd test &&
		git config core.repositoryformatversion >../actual2
	) &&
	test_cmp expect actual &&
	test_cmp expect actual2
'

test_expect_success 'gitdir selection on unsupported repo' '
	# Make sure it would stop at test2, not trash
	echo 99 >expect &&
	(
		cd test2 &&
		git config core.repositoryformatversion >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'gitdir not required mode' '
	git apply --stat test.patch &&
	(
		cd test &&
		git apply --stat ../test.patch
	) &&
	(
		cd test2 &&
		git apply --stat ../test.patch
	)
'

test_expect_success 'gitdir required mode' '
	git apply --check --index test.patch &&
	(
		cd test &&
		git apply --check --index ../test.patch
	) &&
	(
		cd test2 &&
		test_must_fail git apply --check --index ../test.patch
	)
'

test_done
