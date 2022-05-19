#!/bin/sh

test_description='but status rename detection options'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	echo 1 >original &&
	but add . &&
	but cummit -m"Adding original file." &&
	mv original renamed &&
	echo 2 >> renamed &&
	but add . &&
	cat >.butignore <<-\EOF
	.butignore
	expect*
	actual*
	EOF
'

test_expect_success 'status no-options' '
	but status >actual &&
	test_i18ngrep "renamed:" actual
'

test_expect_success 'status --no-renames' '
	but status --no-renames >actual &&
	test_i18ngrep "deleted:" actual &&
	test_i18ngrep "new file:" actual
'

test_expect_success 'status.renames inherits from diff.renames false' '
	but -c diff.renames=false status >actual &&
	test_i18ngrep "deleted:" actual &&
	test_i18ngrep "new file:" actual
'

test_expect_success 'status.renames inherits from diff.renames true' '
	but -c diff.renames=true status >actual &&
	test_i18ngrep "renamed:" actual
'

test_expect_success 'status.renames overrides diff.renames false' '
	but -c diff.renames=true -c status.renames=false status >actual &&
	test_i18ngrep "deleted:" actual &&
	test_i18ngrep "new file:" actual
'

test_expect_success 'status.renames overrides from diff.renames true' '
	but -c diff.renames=false -c status.renames=true status >actual &&
	test_i18ngrep "renamed:" actual
'

test_expect_success 'status status.renames=false' '
	but -c status.renames=false status >actual &&
	test_i18ngrep "deleted:" actual &&
	test_i18ngrep "new file:" actual
'

test_expect_success 'status status.renames=true' '
	but -c status.renames=true status >actual &&
	test_i18ngrep "renamed:" actual
'

test_expect_success 'commit honors status.renames=false' '
	but -c status.renames=false cummit --dry-run >actual &&
	test_i18ngrep "deleted:" actual &&
	test_i18ngrep "new file:" actual
'

test_expect_success 'commit honors status.renames=true' '
	but -c status.renames=true cummit --dry-run >actual &&
	test_i18ngrep "renamed:" actual
'

test_expect_success 'status config overridden' '
	but -c status.renames=true status --no-renames >actual &&
	test_i18ngrep "deleted:" actual &&
	test_i18ngrep "new file:" actual
'

test_expect_success 'status score=100%' '
	but status -M=100% >actual &&
	test_i18ngrep "deleted:" actual &&
	test_i18ngrep "new file:" actual &&

	but status --find-renames=100% >actual &&
	test_i18ngrep "deleted:" actual &&
	test_i18ngrep "new file:" actual
'

test_expect_success 'status score=01%' '
	but status -M=01% >actual &&
	test_i18ngrep "renamed:" actual &&

	but status --find-renames=01% >actual &&
	test_i18ngrep "renamed:" actual
'

test_expect_success 'copies not overridden by find-renames' '
	cp renamed copy &&
	but add copy &&

	but -c status.renames=copies status -M=01% >actual &&
	test_i18ngrep "copied:" actual &&
	test_i18ngrep "renamed:" actual &&

	but -c status.renames=copies status --find-renames=01% >actual &&
	test_i18ngrep "copied:" actual &&
	test_i18ngrep "renamed:" actual
'

test_done
