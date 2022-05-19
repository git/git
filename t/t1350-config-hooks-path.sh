#!/bin/sh

test_description='Test the core.hooksPath configuration variable'

. ./test-lib.sh

test_expect_success 'set up a pre-commit hook in core.hooksPath' '
	>actual &&
	mkdir -p .but/custom-hooks &&
	write_script .but/custom-hooks/pre-cummit <<-\EOF &&
	echo CUSTOM >>actual
	EOF
	test_hook --setup pre-cummit <<-\EOF
	echo NORMAL >>actual
	EOF
'

test_expect_success 'Check that various forms of specifying core.hooksPath work' '
	test_cummit no_custom_hook &&
	but config core.hooksPath .but/custom-hooks &&
	test_commit have_custom_hook &&
	but config core.hooksPath .but/custom-hooks/ &&
	test_commit have_custom_hook_trailing_slash &&
	but config core.hooksPath "$PWD/.but/custom-hooks" &&
	test_commit have_custom_hook_abs_path &&
	but config core.hooksPath "$PWD/.but/custom-hooks/" &&
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

test_expect_success 'but rev-parse --but-path hooks' '
	but config core.hooksPath .but/custom-hooks &&
	but rev-parse --but-path hooks/abc >actual &&
	test .but/custom-hooks/abc = "$(cat actual)"
'

test_done
