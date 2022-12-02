#!/bin/sh

test_description='basic clone options'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '

	mkdir parent &&
	(cd parent && git init &&
	 echo one >file && git add file &&
	 git commit -m one) &&
	git clone --depth=1 --no-local parent shallow-repo

'

test_expect_success 'submodule.stickyRecursiveClone flag manipulates submodule.recurse value' '

	test_config_global submodule.stickyRecursiveClone true &&
	git clone --recurse-submodules parent clone_recurse_true &&
	test_cmp_config -C clone_recurse_true true submodule.recurse &&

	test_config_global submodule.stickyRecursiveClone false &&
	git clone --recurse-submodules parent clone_recurse_false &&
	test_expect_code 1 git -C clone_recurse_false config --get submodule.recurse

'

test_expect_success 'clone -o' '

	git clone -o foo parent clone-o &&
	git -C clone-o rev-parse --verify refs/remotes/foo/main

'

test_expect_success 'rejects invalid -o/--origin' '

	test_must_fail git clone -o "bad...name" parent clone-bad-name 2>err &&
	test_i18ngrep "'\''bad...name'\'' is not a valid remote name" err

'

test_expect_success 'clone --bare -o' '

	git clone -o foo --bare parent clone-bare-o &&
	(cd parent && pwd) >expect &&
	git -C clone-bare-o config remote.foo.url >actual &&
	test_cmp expect actual

'

test_expect_success 'disallows --bare with --separate-git-dir' '

	test_must_fail git clone --bare --separate-git-dir dot-git-destiation parent clone-bare-sgd 2>err &&
	test_debug "cat err" &&
	test_i18ngrep -e "options .--bare. and .--separate-git-dir. cannot be used together" err

'

test_expect_success 'disallows --bundle-uri with shallow options' '
	for option in --depth=1 --shallow-since=01-01-2000 --shallow-exclude=HEAD
	do
		test_must_fail git clone --bundle-uri=bundle $option from to 2>err &&
		grep "bundle-uri is incompatible" err || return 1
	done
'

test_expect_success 'reject cloning shallow repository' '
	test_when_finished "rm -rf repo" &&
	test_must_fail git clone --reject-shallow shallow-repo out 2>err &&
	test_i18ngrep -e "source repository is shallow, reject to clone." err &&

	git clone --no-reject-shallow shallow-repo repo
'

test_expect_success 'reject cloning non-local shallow repository' '
	test_when_finished "rm -rf repo" &&
	test_must_fail git clone --reject-shallow --no-local shallow-repo out 2>err &&
	test_i18ngrep -e "source repository is shallow, reject to clone." err &&

	git clone --no-reject-shallow --no-local shallow-repo repo
'

test_expect_success 'succeed cloning normal repository' '
	test_when_finished "rm -rf chilad1 child2 child3 child4 " &&
	git clone --reject-shallow parent child1 &&
	git clone --reject-shallow --no-local parent child2 &&
	git clone --no-reject-shallow parent child3 &&
	git clone --no-reject-shallow --no-local parent child4
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

test_expect_success 'clone does not segfault with --bare and core.bare=false' '
	test_config_global core.bare false &&
	git clone --bare parent clone-bare &&
	echo true >expect &&
	git -C clone-bare rev-parse --is-bare-repository >actual &&
	test_cmp expect actual
'

test_expect_success 'chooses correct default initial branch name' '
	GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME= \
	git -c init.defaultBranch=foo init --bare empty &&
	test_config -C empty lsrefs.unborn advertise &&
	GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME= \
	git -c init.defaultBranch=up -c protocol.version=2 clone empty whats-up &&
	test refs/heads/foo = $(git -C whats-up symbolic-ref HEAD) &&
	test refs/heads/foo = $(git -C whats-up config branch.foo.merge)
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
