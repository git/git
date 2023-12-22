#!/bin/sh

test_description='test status when slow untracked files'

. ./test-lib.sh

GIT_TEST_UF_DELAY_WARNING=1
export GIT_TEST_UF_DELAY_WARNING

test_expect_success setup '
	git checkout -b test &&
	cat >.gitignore <<-\EOF &&
	/actual
	/expected
	/out
	EOF
	git add .gitignore &&
	git commit -m "Add .gitignore"
'

test_expect_success 'when core.untrackedCache and fsmonitor are unset' '
	test_might_fail git config --unset-all core.untrackedCache &&
	test_might_fail git config --unset-all core.fsmonitor &&
	git status >out &&
	sed "s/[0-9]\.[0-9][0-9]/X/g" out >actual &&
	cat >expected <<-\EOF &&
	On branch test

	It took X seconds to enumerate untracked files.
	See '"'"'git help status'"'"' for information on how to improve this.

	nothing to commit, working tree clean
	EOF
	test_cmp expected actual
'

test_expect_success 'when core.untrackedCache true, but not fsmonitor' '
	test_config core.untrackedCache true &&
	test_might_fail git config --unset-all core.fsmonitor &&
	git status >out &&
	sed "s/[0-9]\.[0-9][0-9]/X/g" out >actual &&
	cat >expected <<-\EOF &&
	On branch test

	It took X seconds to enumerate untracked files.
	See '"'"'git help status'"'"' for information on how to improve this.

	nothing to commit, working tree clean
	EOF
	test_cmp expected actual
'

test_expect_success 'when core.untrackedCache true, and fsmonitor' '
	test_config core.untrackedCache true &&
	test_config core.fsmonitor true &&
	git status >out &&
	sed "s/[0-9]\.[0-9][0-9]/X/g" out >actual &&
	cat >expected <<-\EOF &&
	On branch test

	It took X seconds to enumerate untracked files,
	but the results were cached, and subsequent runs may be faster.
	See '"'"'git help status'"'"' for information on how to improve this.

	nothing to commit, working tree clean
	EOF
	test_cmp expected actual
'

test_done
