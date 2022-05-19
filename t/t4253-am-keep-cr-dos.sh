#!/bin/sh
#
# Copyright (c) 2010 Stefan-W. Hahn
#

test_description='but-am mbox with dos line ending.

'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# Three patches which will be added as files with dos line ending.

cat >file1 <<\EOF
line 1
EOF

cat >file1a <<\EOF
line 1
line 4
EOF

cat >file2 <<\EOF
line 1
line 2
EOF

cat >file3 <<\EOF
line 1
line 2
line 3
EOF

test_expect_success 'setup repository with dos files' '
	append_cr <file1 >file &&
	but add file &&
	but cummit -m Initial &&
	but tag initial &&
	append_cr <file2 >file &&
	but cummit -a -m Second &&
	append_cr <file3 >file &&
	but cummit -a -m Third
'

test_expect_success 'am with dos files without --keep-cr' '
	but checkout -b dosfiles initial &&
	but format-patch -k initial..main &&
	test_must_fail but am -k -3 000*.patch &&
	but am --abort &&
	rm -rf .but/rebase-apply 000*.patch
'

test_expect_success 'am with dos files with --keep-cr' '
	but checkout -b dosfiles-keep-cr initial &&
	but format-patch -k --stdout initial..main >output &&
	but am --keep-cr -k -3 output &&
	but diff --exit-code main
'

test_expect_success 'am with dos files config am.keepcr' '
	but config am.keepcr 1 &&
	but checkout -b dosfiles-conf-keepcr initial &&
	but format-patch -k --stdout initial..main >output &&
	but am -k -3 output &&
	but diff --exit-code main
'

test_expect_success 'am with dos files config am.keepcr overridden by --no-keep-cr' '
	but config am.keepcr 1 &&
	but checkout -b dosfiles-conf-keepcr-override initial &&
	but format-patch -k initial..main &&
	test_must_fail but am -k -3 --no-keep-cr 000*.patch &&
	but am --abort &&
	rm -rf .but/rebase-apply 000*.patch
'

test_expect_success 'am with dos files with --keep-cr continue' '
	but checkout -b dosfiles-keep-cr-continue initial &&
	but format-patch -k initial..main &&
	append_cr <file1a >file &&
	but cummit -m "different patch" file &&
	test_must_fail but am --keep-cr -k -3 000*.patch &&
	append_cr <file2 >file &&
	but add file &&
	but am -3 --resolved &&
	but diff --exit-code main
'

test_expect_success 'am with unix files config am.keepcr overridden by --no-keep-cr' '
	but config am.keepcr 1 &&
	but checkout -b unixfiles-conf-keepcr-override initial &&
	cp -f file1 file &&
	but cummit -m "line ending to unix" file &&
	but format-patch -k initial..main &&
	but am -k -3 --no-keep-cr 000*.patch &&
	but diff --exit-code -w main
'

test_done
