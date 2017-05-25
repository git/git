#!/bin/sh

test_description='rabassa can handle submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh
. "$TEST_DIRECTORY"/lib-rabassa.sh

git_rabassa () {
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
	git rabassa "$1"
}

test_submodule_switch "git_rabassa"

git_rabassa_interactive () {
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
	git rabassa -i "$1"
}

test_submodule_switch "git_rabassa_interactive"

test_done
