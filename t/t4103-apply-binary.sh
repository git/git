#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git apply handling binary patches

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

git update-index --add --remove file1 file2 file4
git-commit -m 'Initial Version' 2>/dev/null

git-checkout -b binary
perl -pe 'y/x/\000/' <file1 >file3
cat file3 >file4
git add file2
perl -pe 'y/\000/v/' <file3 >file1
rm -f file2
git update-index --add --remove file1 file2 file3 file4
git-commit -m 'Second Version'

git diff-tree -p master binary >B.diff
git diff-tree -p -C master binary >C.diff

git diff-tree -p --binary master binary >BF.diff
git diff-tree -p --binary -C master binary >CF.diff

test_expect_success 'stat binary diff -- should not fail.' \
	'git-checkout master
	 git apply --stat --summary B.diff'

test_expect_success 'stat binary diff (copy) -- should not fail.' \
	'git-checkout master
	 git apply --stat --summary C.diff'

test_expect_success 'check binary diff -- should fail.' \
	'git-checkout master &&
	 ! git apply --check B.diff'

test_expect_success 'check binary diff (copy) -- should fail.' \
	'git-checkout master &&
	 ! git apply --check C.diff'

test_expect_success \
	'check incomplete binary diff with replacement -- should fail.' '
	git-checkout master &&
	! git apply --check --allow-binary-replacement B.diff
'

test_expect_success \
    'check incomplete binary diff with replacement (copy) -- should fail.' '
	 git-checkout master &&
	 ! git apply --check --allow-binary-replacement C.diff
'

test_expect_success 'check binary diff with replacement.' \
	'git-checkout master
	 git apply --check --allow-binary-replacement BF.diff'

test_expect_success 'check binary diff with replacement (copy).' \
	'git-checkout master
	 git apply --check --allow-binary-replacement CF.diff'

# Now we start applying them.

do_reset () {
	rm -f file? &&
	git-reset --hard &&
	git-checkout -f master
}

test_expect_success 'apply binary diff -- should fail.' \
	'do_reset &&
	 ! git apply B.diff'

test_expect_success 'apply binary diff -- should fail.' \
	'do_reset &&
	 ! git apply --index B.diff'

test_expect_success 'apply binary diff (copy) -- should fail.' \
	'do_reset &&
	 ! git apply C.diff'

test_expect_success 'apply binary diff (copy) -- should fail.' \
	'do_reset &&
	 ! git apply --index C.diff'

test_expect_success 'apply binary diff without replacement.' \
	'do_reset &&
	 git apply BF.diff'

test_expect_success 'apply binary diff without replacement (copy).' \
	'do_reset &&
	 git apply CF.diff'

test_expect_success 'apply binary diff.' \
	'do_reset &&
	 git apply --allow-binary-replacement --index BF.diff &&
	 test -z "$(git diff --name-status binary)"'

test_expect_success 'apply binary diff (copy).' \
	'do_reset &&
	 git apply --allow-binary-replacement --index CF.diff &&
	 test -z "$(git diff --name-status binary)"'

test_done
