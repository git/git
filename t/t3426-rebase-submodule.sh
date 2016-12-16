#!/bin/sh

test_description='rebase can handle submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

git_rebase () {
	git status -su >expect &&
	ls -1pR * >>expect &&
	git checkout -b ours HEAD &&
	echo x >>file1 &&
	git add file1 &&
	git commit -m add_x &&
	git revert HEAD &&
	git status -su >actual &&
	ls -1pR * >>actual &&
	test_cmp expect actual &&
	git rebase "$1"
}

test_submodule_switch "git_rebase"

git_rebase_interactive () {
	git status -su >expect &&
	ls -1pR * >>expect &&
	git checkout -b ours HEAD &&
	echo x >>file1 &&
	git add file1 &&
	git commit -m add_x &&
	git revert HEAD &&
	git status -su >actual &&
	ls -1pR * >>actual &&
	test_cmp expect actual &&
	set_fake_editor &&
	echo "fake-editor.sh" >.git/info/exclude &&
	git rebase -i "$1"
}

test_submodule_switch "git_rebase_interactive"

test_done
