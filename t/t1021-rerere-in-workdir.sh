#!/bin/sh

test_description='rerere run in a workdir'
. ./test-lib.sh

test_expect_success SYMLINKS setup '
	git config rerere.enabled true &&
	>world &&
	git add world &&
	test_tick &&
	git commit -m initial &&

	echo hello >world &&
	test_tick &&
	git commit -a -m hello &&

	git checkout -b side HEAD^ &&
	echo goodbye >world &&
	test_tick &&
	git commit -a -m goodbye &&

	git checkout master
'

test_expect_success SYMLINKS 'rerere in workdir' '
	rm -rf .git/rr-cache &&
	"$SHELL_PATH" "$TEST_DIRECTORY/../contrib/workdir/git-new-workdir" . work &&
	(
		cd work &&
		test_must_fail git merge side &&
		git rerere status >actual &&
		echo world >expect &&
		test_cmp expect actual
	)
'

# This fails because we don't resolve relative symlink in mkdir_in_gitdir()
# For the purpose of helping contrib/workdir/git-new-workdir users, we do not
# have to support relative symlinks, but it might be nicer to make this work
# with a relative symbolic link someday.
test_expect_failure SYMLINKS 'rerere in workdir (relative)' '
	rm -rf .git/rr-cache &&
	"$SHELL_PATH" "$TEST_DIRECTORY/../contrib/workdir/git-new-workdir" . krow &&
	(
		cd krow &&
		rm -f .git/rr-cache &&
		ln -s ../.git/rr-cache .git/rr-cache &&
		test_must_fail git merge side &&
		git rerere status >actual &&
		echo world >expect &&
		test_cmp expect actual
	)
'

test_done
