#!/bin/sh

test_description='rerere run in a workdir'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success SYMLINKS setup '
	but config rerere.enabled true &&
	>world &&
	but add world &&
	test_tick &&
	but cummit -m initial &&

	echo hello >world &&
	test_tick &&
	but cummit -a -m hello &&

	but checkout -b side HEAD^ &&
	echo goodbye >world &&
	test_tick &&
	but cummit -a -m goodbye &&

	but checkout main
'

test_expect_success SYMLINKS 'rerere in workdir' '
	rm -rf .but/rr-cache &&
	"$SHELL_PATH" "$TEST_DIRECTORY/../contrib/workdir/but-new-workdir" . work &&
	(
		cd work &&
		test_must_fail but merge side &&
		but rerere status >actual &&
		echo world >expect &&
		test_cmp expect actual
	)
'

# This fails because we don't resolve relative symlink in mkdir_in_butdir()
# For the purpose of helping contrib/workdir/but-new-workdir users, we do not
# have to support relative symlinks, but it might be nicer to make this work
# with a relative symbolic link someday.
test_expect_failure SYMLINKS 'rerere in workdir (relative)' '
	rm -rf .but/rr-cache &&
	"$SHELL_PATH" "$TEST_DIRECTORY/../contrib/workdir/but-new-workdir" . krow &&
	(
		cd krow &&
		rm -f .but/rr-cache &&
		ln -s ../.but/rr-cache .but/rr-cache &&
		test_must_fail but merge side &&
		but rerere status >actual &&
		echo world >expect &&
		test_cmp expect actual
	)
'

test_done
