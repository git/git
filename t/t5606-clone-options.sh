#!/bin/sh

test_description='basic clone options'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '

	mkdir parent &&
	(cd parent && but init &&
	 echo one >file && but add file &&
	 but cummit -m one) &&
	but clone --depth=1 --no-local parent shallow-repo

'

test_expect_success 'submodule.stickyRecursiveClone flag manipulates submodule.recurse value' '

	test_config_global submodule.stickyRecursiveClone true &&
	but clone --recurse-submodules parent clone_recurse_true &&
	test_cmp_config -C clone_recurse_true true submodule.recurse &&

	test_config_global submodule.stickyRecursiveClone false &&
	but clone --recurse-submodules parent clone_recurse_false &&
	test_expect_code 1 but -C clone_recurse_false config --get submodule.recurse

'

test_expect_success 'clone -o' '

	but clone -o foo parent clone-o &&
	but -C clone-o rev-parse --verify refs/remotes/foo/main

'

test_expect_success 'rejects invalid -o/--origin' '

	test_must_fail but clone -o "bad...name" parent clone-bad-name 2>err &&
	test_i18ngrep "'\''bad...name'\'' is not a valid remote name" err

'

test_expect_success 'disallows --bare with --origin' '

	test_must_fail but clone -o foo --bare parent clone-bare-o 2>err &&
	test_debug "cat err" &&
	test_i18ngrep -e "options .--bare. and .--origin foo. cannot be used together" err

'

test_expect_success 'disallows --bare with --separate-but-dir' '

	test_must_fail but clone --bare --separate-but-dir dot-but-destiation parent clone-bare-sgd 2>err &&
	test_debug "cat err" &&
	test_i18ngrep -e "options .--bare. and .--separate-but-dir. cannot be used together" err

'

test_expect_success 'reject cloning shallow repository' '
	test_when_finished "rm -rf repo" &&
	test_must_fail but clone --reject-shallow shallow-repo out 2>err &&
	test_i18ngrep -e "source repository is shallow, reject to clone." err &&

	but clone --no-reject-shallow shallow-repo repo
'

test_expect_success 'reject cloning non-local shallow repository' '
	test_when_finished "rm -rf repo" &&
	test_must_fail but clone --reject-shallow --no-local shallow-repo out 2>err &&
	test_i18ngrep -e "source repository is shallow, reject to clone." err &&

	but clone --no-reject-shallow --no-local shallow-repo repo
'

test_expect_success 'succeed cloning normal repository' '
	test_when_finished "rm -rf chilad1 child2 child3 child4 " &&
	but clone --reject-shallow parent child1 &&
	but clone --reject-shallow --no-local parent child2 &&
	but clone --no-reject-shallow parent child3 &&
	but clone --no-reject-shallow --no-local parent child4
'

test_expect_success 'uses "origin" for default remote name' '

	but clone parent clone-default-origin &&
	but -C clone-default-origin rev-parse --verify refs/remotes/origin/main

'

test_expect_success 'prefers --template config over normal config' '

	template="$TRASH_DIRECTORY/template-with-config" &&
	mkdir "$template" &&
	but config --file "$template/config" foo.bar from_template &&
	test_config_global foo.bar from_global &&
	but clone "--template=$template" parent clone-template-config &&
	test "$(but -C clone-template-config config --local foo.bar)" = "from_template"

'

test_expect_success 'prefers -c config over --template config' '

	template="$TRASH_DIRECTORY/template-with-ignored-config" &&
	mkdir "$template" &&
	but config --file "$template/config" foo.bar from_template &&
	but clone "--template=$template" -c foo.bar=inline parent clone-template-inline-config &&
	test "$(but -C clone-template-inline-config config --local foo.bar)" = "inline"

'

test_expect_success 'prefers config "clone.defaultRemoteName" over default' '

	test_config_global clone.defaultRemoteName from_config &&
	but clone parent clone-config-origin &&
	but -C clone-config-origin rev-parse --verify refs/remotes/from_config/main

'

test_expect_success 'prefers --origin over -c config' '

	but clone -c clone.defaultRemoteName=inline --origin from_option parent clone-o-and-inline-config &&
	but -C clone-o-and-inline-config rev-parse --verify refs/remotes/from_option/main

'

test_expect_success 'redirected clone does not show progress' '

	but clone "file://$(pwd)/parent" clone-redirected >out 2>err &&
	! grep % err &&
	test_i18ngrep ! "Checking connectivity" err

'

test_expect_success 'redirected clone -v does show progress' '

	but clone --progress "file://$(pwd)/parent" clone-redirected-progress \
		>out 2>err &&
	grep % err

'

test_expect_success 'clone does not segfault with --bare and core.bare=false' '
	test_config_global core.bare false &&
	but clone --bare parent clone-bare &&
	echo true >expect &&
	but -C clone-bare rev-parse --is-bare-repository >actual &&
	test_cmp expect actual
'

test_expect_success 'chooses correct default initial branch name' '
	BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME= \
	but -c init.defaultBranch=foo init --bare empty &&
	test_config -C empty lsrefs.unborn advertise &&
	BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME= \
	but -c init.defaultBranch=up -c protocol.version=2 clone empty whats-up &&
	test refs/heads/foo = $(but -C whats-up symbolic-ref HEAD) &&
	test refs/heads/foo = $(but -C whats-up config branch.foo.merge)
'

test_expect_success 'guesses initial branch name correctly' '
	but init --initial-branch=guess initial-branch &&
	test_cummit -C initial-branch no-spoilers &&
	but -C initial-branch branch abc guess &&
	but clone initial-branch is-it &&
	test refs/heads/guess = $(but -C is-it symbolic-ref HEAD) &&

	but -c init.defaultBranch=none init --bare no-head &&
	but -C initial-branch push ../no-head guess abc &&
	BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME= \
	but clone no-head is-it2 &&
	test_must_fail but -C is-it2 symbolic-ref refs/remotes/origin/HEAD &&
	but -C no-head update-ref --no-deref HEAD refs/heads/guess &&
	BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME= \
	but -c init.defaultBranch=guess clone no-head is-it3 &&
	test refs/remotes/origin/guess = \
		$(but -C is-it3 symbolic-ref refs/remotes/origin/HEAD)
'

test_done
