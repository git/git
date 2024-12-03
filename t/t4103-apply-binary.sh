#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git apply handling binary patches

'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	cat >file1 <<-\EOF &&
	A quick brown fox jumps over the lazy dog.
	A tiny little penguin runs around in circles.
	There is a flag with Linux written on it.
	A slow black-and-white panda just sits there,
	munching on his bamboo.
	EOF
	cat file1 >file2 &&
	cat file1 >file4 &&

	git update-index --add --remove file1 file2 file4 &&
	git commit -m "Initial Version" 2>/dev/null &&

	git checkout -b binary &&
	perl -pe "y/x/\000/" <file1 >file3 &&
	cat file3 >file4 &&
	git add file2 &&
	perl -pe "y/\000/v/" <file3 >file1 &&
	rm -f file2 &&
	git update-index --add --remove file1 file2 file3 file4 &&
	git commit -m "Second Version" &&

	git diff-tree -p main binary >B.diff &&
	git diff-tree -p -C main binary >C.diff &&

	git diff-tree -p --binary main binary >BF.diff &&
	git diff-tree -p --binary -C main binary >CF.diff &&

	git diff-tree -p --full-index main binary >B-index.diff &&
	git diff-tree -p -C --full-index main binary >C-index.diff &&

	git diff-tree -p --binary --no-prefix main binary -- file3 >B0.diff &&

	git init other-repo &&
	(
		cd other-repo &&
		git fetch .. main &&
		git reset --hard FETCH_HEAD
	)
'

test_expect_success 'stat binary diff -- should not fail.' \
	'git checkout main &&
	 git apply --stat --summary B.diff'

test_expect_success 'stat binary -p0 diff -- should not fail.' '
	 git checkout main &&
	 git apply --stat -p0 B0.diff
'

test_expect_success 'stat binary diff (copy) -- should not fail.' \
	'git checkout main &&
	 git apply --stat --summary C.diff'

test_expect_success 'check binary diff -- should fail.' \
	'git checkout main &&
	 test_must_fail git apply --check B.diff'

test_expect_success 'check binary diff (copy) -- should fail.' \
	'git checkout main &&
	 test_must_fail git apply --check C.diff'

test_expect_success \
	'check incomplete binary diff with replacement -- should fail.' '
	git checkout main &&
	test_must_fail git apply --check --allow-binary-replacement B.diff
'

test_expect_success \
    'check incomplete binary diff with replacement (copy) -- should fail.' '
	 git checkout main &&
	 test_must_fail git apply --check --allow-binary-replacement C.diff
'

test_expect_success 'check binary diff with replacement.' \
	'git checkout main &&
	 git apply --check --allow-binary-replacement BF.diff'

test_expect_success 'check binary diff with replacement (copy).' \
	'git checkout main &&
	 git apply --check --allow-binary-replacement CF.diff'

# Now we start applying them.

do_reset () {
	rm -f file? &&
	git reset --hard &&
	git checkout -f main
}

test_expect_success 'apply binary diff -- should fail.' \
	'do_reset &&
	 test_must_fail git apply B.diff'

test_expect_success 'apply binary diff -- should fail.' \
	'do_reset &&
	 test_must_fail git apply --index B.diff'

test_expect_success 'apply binary diff (copy) -- should fail.' \
	'do_reset &&
	 test_must_fail git apply C.diff'

test_expect_success 'apply binary diff (copy) -- should fail.' \
	'do_reset &&
	 test_must_fail git apply --index C.diff'

test_expect_success 'apply binary diff with full-index' '
	do_reset &&
	git apply B-index.diff
'

test_expect_success 'apply binary diff with full-index (copy)' '
	do_reset &&
	git apply C-index.diff
'

test_expect_success 'apply full-index binary diff in new repo' '
	(cd other-repo &&
	 do_reset &&
	 test_must_fail git apply ../B-index.diff)
'

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

test_expect_success 'apply binary -p0 diff' '
	do_reset &&
	git apply -p0 --index B0.diff &&
	test -z "$(git diff --name-status binary -- file3)"
'

test_expect_success 'reject truncated binary diff' '
	do_reset &&

	# this length is calculated to get us very close to
	# the 8192-byte strbuf we will use to read in the patch.
	test-tool genrandom foo 6205 >file1 &&
	git diff --binary >patch &&

	# truncate the patch at the second "literal" line,
	# but exclude the trailing newline. We must use perl
	# for this, since tools like "sed" cannot reliably
	# produce output without the trailing newline.
	perl -pe "
		if (/^literal/ && \$count++ >= 1) {
			chomp;
			print;
			exit 0;
		}
	" <patch >patch.trunc &&

	do_reset &&
	test_must_fail git apply patch.trunc
'
test_done
