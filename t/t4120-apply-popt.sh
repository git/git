#!/bin/sh
#
# Copyright (c) 2007 Shawn O. Pearce
#

test_description='git apply -p handling.'

. ./test-lib.sh

test_expect_success setup '
	mkdir sub &&
	echo A >sub/file1 &&
	cp sub/file1 file1.saved &&
	git add sub/file1 &&
	echo B >sub/file1 &&
	git diff >patch.file &&
	git checkout -- sub/file1 &&
	git mv sub süb &&
	echo B >süb/file1 &&
	git diff >patch.escaped &&
	grep "[\]" patch.escaped &&
	rm süb/file1 &&
	rmdir süb
'

test_expect_success 'git apply -p 1 patch' '
	cat >patch <<-\EOF &&
	From 90ad11d5b2d437e82d4d992f72fb44c2227798b5 Mon Sep 17 00:00:00 2001
	From: Mroik <mroik@delayed.space>
	Date: Mon, 9 Mar 2026 23:25:00 +0100
	Subject: [PATCH] Test

	---
	 t/test/test | 0
	 1 file changed, 0 insertions(+), 0 deletions(-)
	 create mode 100644 t/test/test

	diff --git a/t/test/test b/t/test/test
	new file mode 100644
	index 0000000000..e69de29bb2
	--
	2.53.0.851.ga537e3e6e9
	EOF
	test_when_finished "rm -rf t" &&
	git apply -p 1 patch &&
	test_path_is_dir t
'

test_expect_success 'apply fails due to non-num -p' '
	test_when_finished "rm -rf t test err" &&
	test_must_fail git apply -p malformed patch 2>err &&
	test_grep "option -p expects a non-negative integer" err
'

test_expect_success 'apply fails due to trailing non-digit in -p' '
	test_when_finished "rm -rf t test err" &&
	test_must_fail git apply -p 2q patch 2>err &&
	test_grep "option -p expects a non-negative integer" err
'

test_expect_success 'apply fails due to negative number in -p' '
	test_when_finished "rm -rf t test err patch" &&
	test_must_fail git apply -p -1 patch 2> err &&
	test_grep "option -p expects a non-negative integer" err
'

test_expect_success 'apply git diff with -p2' '
	cp file1.saved file1 &&
	git apply -p2 patch.file
'

test_expect_success 'apply with too large -p' '
	cp file1.saved file1 &&
	test_must_fail git apply --stat -p3 patch.file 2>err &&
	test_grep "removing 3 leading" err
'

test_expect_success 'apply (-p2) traditional diff with funny filenames' '
	cat >patch.quotes <<-\EOF &&
	diff -u "a/"sub/file1 "b/"sub/file1
	--- "a/"sub/file1
	+++ "b/"sub/file1
	@@ -1 +1 @@
	-A
	+B
	EOF
	echo B >expected &&

	cp file1.saved file1 &&
	git apply -p2 patch.quotes &&
	test_cmp expected file1
'

test_expect_success 'apply with too large -p and fancy filename' '
	cp file1.saved file1 &&
	test_must_fail git apply --stat -p3 patch.escaped 2>err &&
	test_grep "removing 3 leading" err
'

test_expect_success 'apply (-p2) diff, mode change only' '
	cat >patch.chmod <<-\EOF &&
	diff --git a/sub/file1 b/sub/file1
	old mode 100644
	new mode 100755
	EOF
	test_chmod -x file1 &&
	git apply --index -p2 patch.chmod &&
	case $(git ls-files -s file1) in 100755*) : good;; *) false;; esac
'

test_expect_success FILEMODE 'file mode was changed' '
	test -x file1
'

test_expect_success 'apply (-p2) diff, rename' '
	cat >patch.rename <<-\EOF &&
	diff --git a/sub/file1 b/sub/file2
	similarity index 100%
	rename from sub/file1
	rename to sub/file2
	EOF
	echo A >expected &&

	cp file1.saved file1 &&
	rm -f file2 &&
	git apply -p2 patch.rename &&
	test_cmp expected file2
'

test_done
