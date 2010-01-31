#!/bin/sh

test_description='Test am with auto.crlf'

. ./test-lib.sh

cat >patchfile <<\EOF
From 38be10072e45dd6b08ce40851e3fca60a31a340b Mon Sep 17 00:00:00 2001
From: Marius Storm-Olsen <x@y.com>
Date: Thu, 23 Aug 2007 13:00:00 +0200
Subject: test1

---
 foo |    1 +
 1 files changed, 1 insertions(+), 0 deletions(-)
 create mode 100644 foo

diff --git a/foo b/foo
new file mode 100644
index 0000000000000000000000000000000000000000..5716ca5987cbf97d6bb54920bea6adde242d87e6
--- /dev/null
+++ b/foo
@@ -0,0 +1 @@
+bar
EOF

test_expect_success 'setup' '

	git config core.autocrlf true &&
	echo foo >bar &&
	git add bar &&
	test_tick &&
	git commit -m initial

'

test_expect_success 'am' '

	git am -3 <patchfile &&
	git diff-files --name-status --exit-code

'

test_done
