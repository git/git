#!/bin/sh
#
# Copyright (c) 2016 Dan Aloni
# Copyright (c) 2016 Jeff King
#

test_description='per-repo forced setting of email address'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup a likely user.useConfigOnly use case' '
	# we want to make sure a reflog is written, since that needs
	# a non-strict ident. So be sure we have an actual cummit.
	test_cummit foo &&

	sane_unset BUT_AUTHOR_NAME BUT_AUTHOR_EMAIL &&
	sane_unset BUT_CUMMITTER_NAME BUT_CUMMITTER_EMAIL &&
	but config user.name "test" &&
	but config --global user.useConfigOnly true
'

test_expect_success 'fails cummitting if clone email is not set' '
	test_must_fail but cummit --allow-empty -m msg
'

test_expect_success 'fails cummitting if clone email is not set, but EMAIL set' '
	test_must_fail env EMAIL=test@fail.com but cummit --allow-empty -m msg
'

test_expect_success 'succeeds cummitting if clone email is set' '
	test_config user.email "test@ok.com" &&
	but cummit --allow-empty -m msg
'

test_expect_success 'succeeds cloning if global email is not set' '
	but clone . clone
'

test_expect_success 'set up rebase scenarios' '
	# temporarily enable an actual ident for this setup
	test_config user.email foo@example.com &&
	test_cummit new &&
	but branch side-without-commit HEAD^ &&
	but checkout -b side-with-commit HEAD^ &&
	test_cummit side
'

test_expect_success 'fast-forward rebase does not care about ident' '
	but checkout -B tmp side-without-cummit &&
	but rebase main
'

test_expect_success 'non-fast-forward rebase refuses to write cummits' '
	test_when_finished "but rebase --abort || true" &&
	but checkout -B tmp side-with-cummit &&
	test_must_fail but rebase main
'

test_expect_success 'fast-forward rebase does not care about ident (interactive)' '
	but checkout -B tmp side-without-cummit &&
	but rebase -i main
'

test_expect_success 'non-fast-forward rebase refuses to write cummits (interactive)' '
	test_when_finished "but rebase --abort || true" &&
	but checkout -B tmp side-with-cummit &&
	test_must_fail but rebase -i main
'

test_expect_success 'noop interactive rebase does not care about ident' '
	but checkout -B tmp side-with-cummit &&
	but rebase -i HEAD^
'

test_expect_success 'author.name overrides user.name' '
	test_config user.name user &&
	test_config user.email user@example.com &&
	test_config author.name author &&
	test_cummit author-name-override-user &&
	echo author user@example.com > expected-author &&
	echo user user@example.com > expected-cummitter &&
	but log --format="%an %ae" -1 > actual-author &&
	but log --format="%cn %ce" -1 > actual-cummitter &&
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
	but log --format="%an %ae" -1 > actual-author &&
	but log --format="%cn %ce" -1 > actual-cummitter &&
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
	but log --format="%an %ae" -1 > actual-author &&
	but log --format="%cn %ce" -1 > actual-cummitter &&
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
	but log --format="%an %ae" -1 > actual-author &&
	but log --format="%cn %ce" -1 > actual-cummitter &&
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
	BUT_AUTHOR_NAME=env_author && export BUT_AUTHOR_NAME &&
	BUT_AUTHOR_EMAIL=env_author@example.com && export BUT_AUTHOR_EMAIL &&
	BUT_CUMMITTER_NAME=env_cummit && export BUT_CUMMITTER_NAME &&
	BUT_CUMMITTER_EMAIL=env_cummit@example.com && export BUT_CUMMITTER_EMAIL &&
	test_cummit env-override-conf &&
	echo env_author env_author@example.com > expected-author &&
	echo env_cummit env_cummit@example.com > expected-cummitter &&
	but log --format="%an %ae" -1 > actual-author &&
	but log --format="%cn %ce" -1 > actual-cummitter &&
	sane_unset BUT_AUTHOR_NAME BUT_AUTHOR_EMAIL &&
	sane_unset BUT_CUMMITTER_NAME BUT_CUMMITTER_EMAIL &&
	test_cmp expected-author actual-author &&
	test_cmp expected-cummitter actual-cummitter
'

test_done
