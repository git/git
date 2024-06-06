#!/bin/sh

test_description='git-am command-line options override saved options'

. ./test-lib.sh

format_patch () {
	git format-patch --stdout -1 "$1" >"$1".eml
}

test_expect_success 'setup' '
	test_commit initial file &&
	test_commit first file &&

	git checkout initial &&
	git mv file file2 &&
	test_tick &&
	git commit -m renamed-file &&
	git tag renamed-file &&

	git checkout -b side initial &&
	test_commit side1 file &&
	test_commit side2 file &&

	format_patch side1 &&
	format_patch side2
'

test_expect_success '--retry fails without in-progress operation' '
	test_must_fail git am --retry 2>err &&
	test_grep "operation not in progress" err
'

test_expect_success '--3way overrides --no-3way' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout renamed-file &&

	# Applying side1 will fail as the file has been renamed.
	test_must_fail git am --no-3way side[12].eml &&
	test_path_is_dir .git/rebase-apply &&
	test_cmp_rev renamed-file HEAD &&
	test -z "$(git ls-files -u)" &&

	# Applying side1 with am --3way will succeed due to the threeway-merge.
	# Applying side2 will fail as --3way does not apply to it.
	test_must_fail git am --retry --3way &&
	test_path_is_dir .git/rebase-apply &&
	test side1 = "$(cat file2)"
'

test_expect_success '--no-quiet overrides --quiet' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&

	# Applying side1 will be quiet.
	test_must_fail git am --quiet side[123].eml >out &&
	test_path_is_dir .git/rebase-apply &&
	test_grep ! "^Applying: " out &&
	echo side1 >file &&
	git add file &&

	# Applying side1 will not be quiet.
	# Applying side2 will be quiet.
	git am --no-quiet --continue >out &&
	echo "Applying: side1" >expected &&
	test_cmp expected out
'

test_expect_success '--signoff overrides --no-signoff' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&

	test_must_fail git am --no-signoff side[12].eml &&
	test_path_is_dir .git/rebase-apply &&
	echo side1 >file &&
	git add file &&
	git am --signoff --continue &&

	# Applied side1 will be signed off
	echo "Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>" >expected &&
	git cat-file commit HEAD^ | grep "Signed-off-by:" >actual &&
	test_cmp expected actual &&

	# Applied side2 will not be signed off
	test $(git cat-file commit HEAD | grep -c "Signed-off-by:") -eq 0
'

test_expect_success '--reject overrides --no-reject' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	rm -f file.rej &&

	test_must_fail git am --no-reject side1.eml &&
	test_path_is_dir .git/rebase-apply &&
	test_path_is_missing file.rej &&

	test_must_fail git am --retry --reject </dev/zero &&
	test_path_is_dir .git/rebase-apply &&
	test_path_is_file file.rej
'

test_done
