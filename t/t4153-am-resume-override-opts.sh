#!/bin/sh

test_description='but-am command-line options override saved options'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

format_patch () {
	but format-patch --stdout -1 "$1" >"$1".eml
}

test_expect_success 'setup' '
	test_cummit initial file &&
	test_cummit first file &&

	but checkout initial &&
	but mv file file2 &&
	test_tick &&
	but cummit -m renamed-file &&
	but tag renamed-file &&

	but checkout -b side initial &&
	test_cummit side1 file &&
	test_cummit side2 file &&

	format_patch side1 &&
	format_patch side2
'

test_expect_success TTY '--3way overrides --no-3way' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout renamed-file &&

	# Applying side1 will fail as the file has been renamed.
	test_must_fail but am --no-3way side[12].eml &&
	test_path_is_dir .but/rebase-apply &&
	test_cmp_rev renamed-file HEAD &&
	test -z "$(but ls-files -u)" &&

	# Applying side1 with am --3way will succeed due to the threeway-merge.
	# Applying side2 will fail as --3way does not apply to it.
	test_must_fail test_terminal but am --3way </dev/zero &&
	test_path_is_dir .but/rebase-apply &&
	test side1 = "$(cat file2)"
'

test_expect_success '--no-quiet overrides --quiet' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&

	# Applying side1 will be quiet.
	test_must_fail but am --quiet side[123].eml >out &&
	test_path_is_dir .but/rebase-apply &&
	test_i18ngrep ! "^Applying: " out &&
	echo side1 >file &&
	but add file &&

	# Applying side1 will not be quiet.
	# Applying side2 will be quiet.
	but am --no-quiet --continue >out &&
	echo "Applying: side1" >expected &&
	test_cmp expected out
'

test_expect_success '--signoff overrides --no-signoff' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&

	test_must_fail but am --no-signoff side[12].eml &&
	test_path_is_dir .but/rebase-apply &&
	echo side1 >file &&
	but add file &&
	but am --signoff --continue &&

	# Applied side1 will be signed off
	echo "Signed-off-by: $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL>" >expected &&
	but cat-file commit HEAD^ | grep "Signed-off-by:" >actual &&
	test_cmp expected actual &&

	# Applied side2 will not be signed off
	test $(but cat-file commit HEAD | grep -c "Signed-off-by:") -eq 0
'

test_expect_success TTY '--reject overrides --no-reject' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	rm -f file.rej &&

	test_must_fail but am --no-reject side1.eml &&
	test_path_is_dir .but/rebase-apply &&
	test_path_is_missing file.rej &&

	test_must_fail test_terminal but am --reject </dev/zero &&
	test_path_is_dir .but/rebase-apply &&
	test_path_is_file file.rej
'

test_done
