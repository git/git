#!/bin/sh
#
# Copyright (c) 2016 Dan Aloni
# Copyright (c) 2016 Jeff King
#

test_description='per-repo forced setting of email address'

. ./test-lib.sh

test_expect_success 'setup a likely user.useConfigOnly use case' '
	# we want to make sure a reflog is written, since that needs
	# a non-strict ident. So be sure we have an actual commit.
	test_commit foo &&

	sane_unset GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL &&
	sane_unset GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL &&
	git config user.name "test" &&
	git config --global user.useConfigOnly true
'

test_expect_success 'fails committing if clone email is not set' '
	test_must_fail git commit --allow-empty -m msg
'

test_expect_success 'fails committing if clone email is not set, but EMAIL set' '
	test_must_fail env EMAIL=test@fail.com git commit --allow-empty -m msg
'

test_expect_success 'succeeds committing if clone email is set' '
	test_config user.email "test@ok.com" &&
	git commit --allow-empty -m msg
'

test_expect_success 'succeeds cloning if global email is not set' '
	git clone . clone
'

test_expect_success 'set up rebase scenarios' '
	# temporarily enable an actual ident for this setup
	test_config user.email foo@example.com &&
	test_commit new &&
	git branch side-without-commit HEAD^ &&
	git checkout -b side-with-commit HEAD^ &&
	test_commit side
'

test_expect_success 'fast-forward rebase does not care about ident' '
	git checkout -B tmp side-without-commit &&
	git rebase master
'

test_expect_success 'non-fast-forward rebase refuses to write commits' '
	test_when_finished "git rebase --abort || true" &&
	git checkout -B tmp side-with-commit &&
	test_must_fail git rebase master
'

test_expect_success 'fast-forward rebase does not care about ident (interactive)' '
	git checkout -B tmp side-without-commit &&
	git rebase -i master
'

test_expect_success 'non-fast-forward rebase refuses to write commits (interactive)' '
	test_when_finished "git rebase --abort || true" &&
	git checkout -B tmp side-with-commit &&
	test_must_fail git rebase -i master
'

test_expect_success 'noop interactive rebase does not care about ident' '
	git checkout -B tmp side-with-commit &&
	git rebase -i HEAD^
'

test_expect_success 'fast-forward rebase does not care about ident (preserve)' '
	git checkout -B tmp side-without-commit &&
	git rebase -p master
'

test_expect_success 'non-fast-forward rebase refuses to write commits (preserve)' '
	test_when_finished "git rebase --abort || true" &&
	git checkout -B tmp side-with-commit &&
	test_must_fail git rebase -p master
'

test_done
