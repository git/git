#!/bin/sh

test_description='git bugreport'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'create a report' '
	git bugreport -s format &&
	test_file_not_empty git-bugreport-format.txt
'

test_expect_success 'report contains wanted template (before first section)' '
	sed -ne "/^\[/q;p" git-bugreport-format.txt >actual &&
	cat >expect <<-\EOF &&
	Thank you for filling out a Git bug report!
	Please answer the following questions to help us understand your issue.

	What did you do before the bug happened? (Steps to reproduce your issue)

	What did you expect to happen? (Expected behavior)

	What happened instead? (Actual behavior)

	What'\''s different between what you expected and what actually happened?

	Anything else you want to add:

	Please review the rest of the bug report below.
	You can delete any lines you don'\''t wish to share.


	EOF
	test_cmp expect actual
'

test_expect_success 'sanity check "System Info" section' '
	test_when_finished rm -f git-bugreport-format.txt &&

	sed -ne "/^\[System Info\]$/,/^$/p" <git-bugreport-format.txt >system &&

	# The beginning should match "git version --build-options" verbatim,
	# but rather than checking bit-for-bit equality, just test some basics.
	grep "git version " system &&
	grep "shell-path: ." system &&

	# After the version, there should be some more info.
	# This is bound to differ from environment to environment,
	# so we just do some rather high-level checks.
	grep "uname: ." system &&
	grep "compiler info: ." system
'

test_expect_success 'dies if file with same name as report already exists' '
	test_when_finished rm git-bugreport-duplicate.txt &&
	>>git-bugreport-duplicate.txt &&
	test_must_fail git bugreport --suffix duplicate
'

test_expect_success '--output-directory puts the report in the provided dir' '
	test_when_finished rm -fr foo/ &&
	git bugreport -o foo/ &&
	test_path_is_file foo/git-bugreport-*
'

test_expect_success 'incorrect arguments abort with usage' '
	test_must_fail git bugreport --false 2>output &&
	test_grep usage output &&
	test_path_is_missing git-bugreport-*
'

test_expect_success 'incorrect positional arguments abort with usage and hint' '
	test_must_fail git bugreport false 2>output &&
	test_grep usage output &&
	test_grep false output &&
	test_path_is_missing git-bugreport-*
'

test_expect_success 'runs outside of a git dir' '
	test_when_finished rm non-repo/git-bugreport-* &&
	nongit git bugreport
'

test_expect_success 'can create leading directories outside of a git dir' '
	test_when_finished rm -fr foo/bar/baz &&
	nongit git bugreport -o foo/bar/baz
'

test_expect_success 'indicates populated hooks' '
	test_when_finished rm git-bugreport-hooks.txt &&

	test_hook applypatch-msg <<-\EOF &&
	true
	EOF
	test_hook unknown-hook <<-\EOF &&
	true
	EOF
	git bugreport -s hooks &&

	sort >expect <<-\EOF &&
	[Enabled Hooks]
	applypatch-msg
	EOF

	sed -ne "/^\[Enabled Hooks\]$/,/^$/p" <git-bugreport-hooks.txt >actual &&
	test_cmp expect actual
'

test_expect_success UNZIP '--diagnose creates diagnostics zip archive' '
	test_when_finished rm -rf report &&

	git bugreport --diagnose -o report -s test >out &&

	zip_path=report/git-diagnostics-test.zip &&
	grep "Available space" out &&
	test_path_is_file "$zip_path" &&

	# Check zipped archive content
	"$GIT_UNZIP" -p "$zip_path" diagnostics.log >out &&
	test_file_not_empty out &&

	"$GIT_UNZIP" -p "$zip_path" packs-local.txt >out &&
	grep ".git/objects" out &&

	"$GIT_UNZIP" -p "$zip_path" objects-local.txt >out &&
	grep "^Total: [0-9][0-9]*" out &&

	# Should not include .git directory contents by default
	! "$GIT_UNZIP" -l "$zip_path" | grep ".git/"
'

test_expect_success UNZIP '--diagnose=stats excludes .git dir contents' '
	test_when_finished rm -rf report &&

	git bugreport --diagnose=stats -o report -s test >out &&

	# Includes pack quantity/size info
	"$GIT_UNZIP" -p "$zip_path" packs-local.txt >out &&
	grep ".git/objects" out &&

	# Does not include .git directory contents
	! "$GIT_UNZIP" -l "$zip_path" | grep ".git/"
'

test_expect_success UNZIP '--diagnose=all includes .git dir contents' '
	test_when_finished rm -rf report &&

	git bugreport --diagnose=all -o report -s test >out &&

	# Includes .git directory contents
	"$GIT_UNZIP" -l "$zip_path" | grep ".git/" &&

	"$GIT_UNZIP" -p "$zip_path" .git/HEAD >out &&
	test_file_not_empty out
'

test_done
