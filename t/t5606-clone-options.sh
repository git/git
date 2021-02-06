#!/bin/sh

test_description='basic clone options'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '

	mkdir parent &&
	(cd parent && git init &&
	 echo one >file && git add file &&
	 git commit -m one)

'

test_expect_success 'clone -o' '

	git clone -o foo parent clone-o &&
	git -C clone-o rev-parse --verify refs/remotes/foo/main

'

test_expect_success 'rejects invalid -o/--origin' '

	test_must_fail git clone -o "bad...name" parent clone-bad-name 2>err &&
	test_i18ngrep "'\''bad...name'\'' is not a valid remote name" err

'

test_expect_success 'disallows --bare with --origin' '

	test_must_fail git clone -o foo --bare parent clone-bare-o 2>err &&
	test_debug "cat err" &&
	test_i18ngrep -e "--bare and --origin foo options are incompatible" err

'

test_expect_success 'disallows --bare with --separate-git-dir' '

	test_must_fail git clone --bare --separate-git-dir dot-git-destiation parent clone-bare-sgd 2>err &&
	test_debug "cat err" &&
	test_i18ngrep -e "--bare and --separate-git-dir are incompatible" err

'

test_expect_success 'uses "origin" for default remote name' '

	git clone parent clone-default-origin &&
	git -C clone-default-origin rev-parse --verify refs/remotes/origin/main

'

test_expect_success 'prefers --template config over normal config' '

	template="$TRASH_DIRECTORY/template-with-config" &&
	mkdir "$template" &&
	git config --file "$template/config" foo.bar from_template &&
	test_config_global foo.bar from_global &&
	git clone "--template=$template" parent clone-template-config &&
	test "$(git -C clone-template-config config --local foo.bar)" = "from_template"

'

test_expect_success 'prefers -c config over --template config' '

	template="$TRASH_DIRECTORY/template-with-ignored-config" &&
	mkdir "$template" &&
	git config --file "$template/config" foo.bar from_template &&
	git clone "--template=$template" -c foo.bar=inline parent clone-template-inline-config &&
	test "$(git -C clone-template-inline-config config --local foo.bar)" = "inline"

'

test_expect_success 'prefers config "clone.defaultRemoteName" over default' '

	test_config_global clone.defaultRemoteName from_config &&
	git clone parent clone-config-origin &&
	git -C clone-config-origin rev-parse --verify refs/remotes/from_config/main

'

test_expect_success 'prefers --origin over -c config' '

	git clone -c clone.defaultRemoteName=inline --origin from_option parent clone-o-and-inline-config &&
	git -C clone-o-and-inline-config rev-parse --verify refs/remotes/from_option/main

'

test_expect_success 'redirected clone does not show progress' '

	git clone "file://$(pwd)/parent" clone-redirected >out 2>err &&
	! grep % err &&
	test_i18ngrep ! "Checking connectivity" err

'

test_expect_success 'redirected clone -v does show progress' '

	git clone --progress "file://$(pwd)/parent" clone-redirected-progress \
		>out 2>err &&
	grep % err

'

test_expect_success 'chooses correct default initial branch name' '
	git init --bare empty &&
	GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME= \
	git -c init.defaultBranch=up clone empty whats-up &&
	test refs/heads/up = $(git -C whats-up symbolic-ref HEAD) &&
	test refs/heads/up = $(git -C whats-up config branch.up.merge)
'

test_expect_success 'guesses initial branch name correctly' '
	git init --initial-branch=guess initial-branch &&
	test_commit -C initial-branch no-spoilers &&
	git -C initial-branch branch abc guess &&
	git clone initial-branch is-it &&
	test refs/heads/guess = $(git -C is-it symbolic-ref HEAD) &&

	git -c init.defaultBranch=none init --bare no-head &&
	git -C initial-branch push ../no-head guess abc &&
	GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME= \
	git clone no-head is-it2 &&
	test_must_fail git -C is-it2 symbolic-ref refs/remotes/origin/HEAD &&
	git -C no-head update-ref --no-deref HEAD refs/heads/guess &&
	GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME= \
	git -c init.defaultBranch=guess clone no-head is-it3 &&
	test refs/remotes/origin/guess = \
		$(git -C is-it3 symbolic-ref refs/remotes/origin/HEAD)
'

test_done
