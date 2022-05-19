#!/bin/sh
#
# Copyright (c) 2016 Dan Aloni
# Copyright (c) 2016 Jeff King
#

test_description='per-repo forced setting of email address'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup a likely user.useConfigOnly use case' '
	# we want to make sure a reflog is written, since that needs
	# a non-strict ident. So be sure we have an actual cummit.
	test_cummit foo &&

	sane_unset GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL &&
	sane_unset GIT_CUMMITTER_NAME GIT_CUMMITTER_EMAIL &&
	git config user.name "test" &&
	git config --global user.useConfigOnly true
'

test_expect_success 'fails cummitting if clone email is not set' '
	test_must_fail git cummit --allow-empty -m msg
'

test_expect_success 'fails cummitting if clone email is not set, but EMAIL set' '
	test_must_fail env EMAIL=test@fail.com git cummit --allow-empty -m msg
'

test_expect_success 'succeeds cummitting if clone email is set' '
	test_config user.email "test@ok.com" &&
	git cummit --allow-empty -m msg
'

test_expect_success 'succeeds cloning if global email is not set' '
	git clone . clone
'

test_expect_success 'set up rebase scenarios' '
	# temporarily enable an actual ident for this setup
	test_config user.email foo@example.com &&
	test_cummit new &&
	git branch side-without-commit HEAD^ &&
	git checkout -b side-with-commit HEAD^ &&
	test_cummit side
'

test_expect_success 'fast-forward rebase does not care about ident' '
	git checkout -B tmp side-without-cummit &&
	git rebase main
'

test_expect_success 'non-fast-forward rebase refuses to write cummits' '
	test_when_finished "git rebase --abort || true" &&
	git checkout -B tmp side-with-cummit &&
	test_must_fail git rebase main
'

test_expect_success 'fast-forward rebase does not care about ident (interactive)' '
	git checkout -B tmp side-without-cummit &&
	git rebase -i main
'

test_expect_success 'non-fast-forward rebase refuses to write cummits (interactive)' '
	test_when_finished "git rebase --abort || true" &&
	git checkout -B tmp side-with-cummit &&
	test_must_fail git rebase -i main
'

test_expect_success 'noop interactive rebase does not care about ident' '
	git checkout -B tmp side-with-cummit &&
	git rebase -i HEAD^
'

test_expect_success 'author.name overrides user.name' '
	test_config user.name user &&
	test_config user.email user@example.com &&
	test_config author.name author &&
	test_cummit author-name-override-user &&
	echo author user@example.com > expected-author &&
	echo user user@example.com > expected-cummitter &&
	git log --format="%an %ae" -1 > actual-author &&
	git log --format="%cn %ce" -1 > actual-cummitter &&
	test_cmp expected-author actual-author &&
	test_cmp expected-cummitter actual-cummitter
'

test_expect_success 'author.email overrides user.email' '
	test_config user.name user &&
	test_config user.email user@example.com &&
	test_config author.email author@example.com &&
	test_cummit author-email-override-user &&
	echo user author@example.com > expected-author &&
	echo user user@example.com > expected-cummitter &&
	git log --format="%an %ae" -1 > actual-author &&
	git log --format="%cn %ce" -1 > actual-cummitter &&
	test_cmp expected-author actual-author &&
	test_cmp expected-cummitter actual-cummitter
'

test_expect_success 'cummitter.name overrides user.name' '
	test_config user.name user &&
	test_config user.email user@example.com &&
	test_config cummitter.name cummitter &&
	test_cummit cummitter-name-override-user &&
	echo user user@example.com > expected-author &&
	echo cummitter user@example.com > expected-cummitter &&
	git log --format="%an %ae" -1 > actual-author &&
	git log --format="%cn %ce" -1 > actual-cummitter &&
	test_cmp expected-author actual-author &&
	test_cmp expected-cummitter actual-cummitter
'

test_expect_success 'cummitter.email overrides user.email' '
	test_config user.name user &&
	test_config user.email user@example.com &&
	test_config cummitter.email cummitter@example.com &&
	test_cummit cummitter-email-override-user &&
	echo user user@example.com > expected-author &&
	echo user cummitter@example.com > expected-cummitter &&
	git log --format="%an %ae" -1 > actual-author &&
	git log --format="%cn %ce" -1 > actual-cummitter &&
	test_cmp expected-author actual-author &&
	test_cmp expected-cummitter actual-cummitter
'

test_expect_success 'author and cummitter environment variables override config settings' '
	test_config user.name user &&
	test_config user.email user@example.com &&
	test_config author.name author &&
	test_config author.email author@example.com &&
	test_config cummitter.name cummitter &&
	test_config cummitter.email cummitter@example.com &&
	GIT_AUTHOR_NAME=env_author && export GIT_AUTHOR_NAME &&
	GIT_AUTHOR_EMAIL=env_author@example.com && export GIT_AUTHOR_EMAIL &&
	GIT_CUMMITTER_NAME=env_cummit && export GIT_CUMMITTER_NAME &&
	GIT_CUMMITTER_EMAIL=env_cummit@example.com && export GIT_CUMMITTER_EMAIL &&
	test_cummit env-override-conf &&
	echo env_author env_author@example.com > expected-author &&
	echo env_cummit env_cummit@example.com > expected-cummitter &&
	git log --format="%an %ae" -1 > actual-author &&
	git log --format="%cn %ce" -1 > actual-cummitter &&
	sane_unset GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL &&
	sane_unset GIT_CUMMITTER_NAME GIT_CUMMITTER_EMAIL &&
	test_cmp expected-author actual-author &&
	test_cmp expected-cummitter actual-cummitter
'

test_done
