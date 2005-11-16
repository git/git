#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git-apply handling binary patches

'
. ./test-lib.sh

# setup

cat >file1 <<EOF
A quick brown fox jumps over the lazy dog.
A tiny little penguin runs around in circles.
There is a flag with Linux written on it.
A slow black-and-white panda just sits there,
munching on his bamboo.
EOF
cat file1 >file2
cat file1 >file4

git-update-index --add --remove file1 file2 file4
git-commit -m 'Initial Version' 2>/dev/null

git-checkout -b binary
tr 'x' '\0' <file1 >file3
cat file3 >file4
git-add file2
tr '\0' 'v' <file3 >file1
rm -f file2
git-update-index --add --remove file1 file2 file3 file4
git-commit -m 'Second Version'

git-diff-tree -p master binary >B.diff
git-diff-tree -p -C master binary >C.diff

test_expect_success 'stat binary diff -- should not fail.' \
	'git-checkout master
	 git-apply --stat --summary B.diff'

test_expect_success 'stat binary diff (copy) -- should not fail.' \
	'git-checkout master
	 git-apply --stat --summary C.diff'

test_expect_failure 'check binary diff -- should fail.' \
	'git-checkout master
	 git-apply --check B.diff'

test_expect_failure 'check binary diff (copy) -- should fail.' \
	'git-checkout master
	 git-apply --check C.diff'

# Now we start applying them.

test_expect_failure 'apply binary diff -- should fail.' \
	'git-checkout master
	 git-apply B.diff'

git-reset --hard

test_expect_failure 'apply binary diff -- should fail.' \
	'git-checkout master
	 git-apply --index B.diff'

git-reset --hard

test_expect_failure 'apply binary diff (copy) -- should fail.' \
	'git-checkout master
	 git-apply C.diff'

git-reset --hard

test_expect_failure 'apply binary diff (copy) -- should fail.' \
	'git-checkout master
	 git-apply --index C.diff'

test_done
