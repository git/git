#!/bin/sh

test_description='Test the core.hooksPath configuration variable'

. ./test-lib.sh

test_expect_success 'set up a pre-commit hook in core.hooksPath' '
	>actual &&
	mkdir -p .git/custom-hooks .git/hooks &&
	write_script .git/custom-hooks/pre-commit <<-\EOF &&
	echo CUSTOM >>actual
	EOF
	write_script .git/hooks/pre-commit <<-\EOF
	echo NORMAL >>actual
	EOF
'

test_expect_success 'Check that various forms of specifying core.hooksPath work' '
	test_commit no_custom_hook &&
	git config core.hooksPath .git/custom-hooks &&
	test_commit have_custom_hook &&
	git config core.hooksPath .git/custom-hooks/ &&
	test_commit have_custom_hook_trailing_slash &&
	git config core.hooksPath "$PWD/.git/custom-hooks" &&
	test_commit have_custom_hook_abs_path &&
	git config core.hooksPath "$PWD/.git/custom-hooks/" &&
	test_commit have_custom_hook_abs_path_trailing_slash &&
	cat >expect <<-\EOF &&
	NORMAL
	CUSTOM
	CUSTOM
	CUSTOM
	CUSTOM
	EOF
	test_cmp expect actual
'

test_expect_success 'git rev-parse --git-path hooks' '
	git config core.hooksPath .git/custom-hooks &&
	git rev-parse --git-path hooks/abc >actual &&
	test .git/custom-hooks/abc = "$(cat actual)"
'

test_done
