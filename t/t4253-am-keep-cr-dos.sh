#!/bin/sh
#
# Copyright (c) 2010 Stefan-W. Hahn
#

test_description='git-am mbox with dos line ending.

'
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
	git add file &&
	git commit -m Initial &&
	git tag initial &&
	append_cr <file2 >file &&
	git commit -a -m Second &&
	append_cr <file3 >file &&
	git commit -a -m Third
'

test_expect_success 'am with dos files without --keep-cr' '
	git checkout -b dosfiles initial &&
	git format-patch -k initial..master &&
	test_must_fail git am -k -3 000*.patch &&
	git am --abort &&
	rm -rf .git/rebase-apply 000*.patch
'

test_expect_success 'am with dos files with --keep-cr' '
	git checkout -b dosfiles-keep-cr initial &&
	git format-patch -k --stdout initial..master | git am --keep-cr -k -3 &&
	git diff --exit-code master
'

test_expect_success 'am with dos files config am.keepcr' '
	git config am.keepcr 1 &&
	git checkout -b dosfiles-conf-keepcr initial &&
	git format-patch -k --stdout initial..master | git am -k -3 &&
	git diff --exit-code master
'

test_expect_success 'am with dos files config am.keepcr overridden by --no-keep-cr' '
	git config am.keepcr 1 &&
	git checkout -b dosfiles-conf-keepcr-override initial &&
	git format-patch -k initial..master &&
	test_must_fail git am -k -3 --no-keep-cr 000*.patch &&
	git am --abort &&
	rm -rf .git/rebase-apply 000*.patch
'

test_expect_success 'am with dos files with --keep-cr continue' '
	git checkout -b dosfiles-keep-cr-continue initial &&
	git format-patch -k initial..master &&
	append_cr <file1a >file &&
	git commit -m "different patch" file &&
	test_must_fail git am --keep-cr -k -3 000*.patch &&
	append_cr <file2 >file &&
	git add file &&
	git am -3 --resolved &&
	git diff --exit-code master
'

test_expect_success 'am with unix files config am.keepcr overridden by --no-keep-cr' '
	git config am.keepcr 1 &&
	git checkout -b unixfiles-conf-keepcr-override initial &&
	cp -f file1 file &&
	git commit -m "line ending to unix" file &&
	git format-patch -k initial..master &&
	git am -k -3 --no-keep-cr 000*.patch &&
	git diff --exit-code -w master
'

test_done
