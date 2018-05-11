#!/bin/sh

test_description='git status rename detection options'

. ./test-lib.sh

test_expect_success 'setup' '
	echo 1 >original &&
	git add . &&
	git commit -m"Adding original file." &&
	mv original renamed &&
	echo 2 >> renamed &&
	git add . &&
	cat >.gitignore <<-\EOF
	.gitignore
	expect*
	actual*
	EOF
'

test_expect_success 'status no-options' '
	git status >actual &&
	test_i18ngrep "renamed:" actual
'

test_expect_success 'status --no-renames' '
	git status --no-renames >actual &&
	test_i18ngrep "deleted:" actual &&
	test_i18ngrep "new file:" actual
'

test_expect_success 'status.renames inherits from diff.renames false' '
	git -c diff.renames=false status >actual &&
	test_i18ngrep "deleted:" actual &&
	test_i18ngrep "new file:" actual
'

test_expect_success 'status.renames inherits from diff.renames true' '
	git -c diff.renames=true status >actual &&
	test_i18ngrep "renamed:" actual
'

test_expect_success 'status.renames overrides diff.renames false' '
	git -c diff.renames=true -c status.renames=false status >actual &&
	test_i18ngrep "deleted:" actual &&
	test_i18ngrep "new file:" actual
'

test_expect_success 'status.renames overrides from diff.renames true' '
	git -c diff.renames=false -c status.renames=true status >actual &&
	test_i18ngrep "renamed:" actual
'

test_expect_success 'status status.renames=false' '
	git -c status.renames=false status >actual &&
	test_i18ngrep "deleted:" actual &&
	test_i18ngrep "new file:" actual
'

test_expect_success 'status status.renames=true' '
	git -c status.renames=true status >actual &&
	test_i18ngrep "renamed:" actual
'

test_expect_success 'commit honors status.renames=false' '
	git -c status.renames=false commit --dry-run >actual &&
	test_i18ngrep "deleted:" actual &&
	test_i18ngrep "new file:" actual
'

test_expect_success 'commit honors status.renames=true' '
	git -c status.renames=true commit --dry-run >actual &&
	test_i18ngrep "renamed:" actual
'

test_expect_success 'status config overridden' '
	git -c status.renames=true status --no-renames >actual &&
	test_i18ngrep "deleted:" actual &&
	test_i18ngrep "new file:" actual
'

test_expect_success 'status score=100%' '
	git status -M=100% >actual &&
	test_i18ngrep "deleted:" actual &&
	test_i18ngrep "new file:" actual &&

	git status --find-rename=100% >actual &&
	test_i18ngrep "deleted:" actual &&
	test_i18ngrep "new file:" actual
'

test_expect_success 'status score=01%' '
	git status -M=01% >actual &&
	test_i18ngrep "renamed:" actual &&

	git status --find-rename=01% >actual &&
	test_i18ngrep "renamed:" actual
'

test_expect_success 'copies not overridden by find-rename' '
	cp renamed copy &&
	git add copy &&

	git -c status.renames=copies status -M=01% >actual &&
	test_i18ngrep "copied:" actual &&
	test_i18ngrep "renamed:" actual &&

	git -c status.renames=copies status --find-rename=01% >actual &&
	test_i18ngrep "copied:" actual &&
	test_i18ngrep "renamed:" actual
'

test_done
