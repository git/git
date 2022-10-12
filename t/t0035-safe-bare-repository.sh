#!/bin/sh

test_description='verify safe.bareRepository checks'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

pwd="$(pwd)"

expect_accepted () {
	git "$@" rev-parse --git-dir
}

expect_rejected () {
	test_must_fail git "$@" rev-parse --git-dir 2>err &&
	grep -F "cannot use bare repository" err
}

test_expect_success 'setup bare repo in worktree' '
	git init outer-repo &&
	git init --bare outer-repo/bare-repo
'

test_expect_success 'safe.bareRepository unset' '
	expect_accepted -C outer-repo/bare-repo
'

test_expect_success 'safe.bareRepository=all' '
	test_config_global safe.bareRepository all &&
	expect_accepted -C outer-repo/bare-repo
'

test_expect_success 'safe.bareRepository=explicit' '
	test_config_global safe.bareRepository explicit &&
	expect_rejected -C outer-repo/bare-repo
'

test_expect_success 'safe.bareRepository in the repository' '
	# safe.bareRepository must not be "explicit", otherwise
	# git config fails with "fatal: not in a git directory" (like
	# safe.directory)
	test_config -C outer-repo/bare-repo safe.bareRepository \
		all &&
	test_config_global safe.bareRepository explicit &&
	expect_rejected -C outer-repo/bare-repo
'

test_expect_success 'safe.bareRepository on the command line' '
	test_config_global safe.bareRepository explicit &&
	expect_accepted -C outer-repo/bare-repo \
		-c safe.bareRepository=all
'

test_expect_success 'safe.bareRepository in included file' '
	cat >gitconfig-include <<-\EOF &&
	[safe]
		bareRepository = explicit
	EOF
	git config --global --add include.path "$(pwd)/gitconfig-include" &&
	expect_rejected -C outer-repo/bare-repo
'

test_done
