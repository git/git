#!/bin/sh

test_description='rebase can handle submodules'

TEST_PASSES_SANITIZE_LEAK=true
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
	may_only_be_test_must_fail "$2" &&
	$2 git rebase "$1"
}

test_submodule_switch_func "git_rebase"

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
	mkdir .git/info &&
	echo "fake-editor.sh" >.git/info/exclude &&
	may_only_be_test_must_fail "$2" &&
	$2 git rebase -i "$1"
}

test_submodule_switch_func "git_rebase_interactive"

test_expect_success 'rebase interactive ignores modified submodules' '
	test_when_finished "rm -rf super sub" &&
	git init sub &&
	git -C sub commit --allow-empty -m "Initial commit" &&
	git init super &&
	git -c protocol.file.allow=always \
		-C super submodule add ../sub &&
	git -C super config submodule.sub.ignore dirty &&
	>super/foo &&
	git -C super add foo &&
	git -C super commit -m "Initial commit" &&
	test_commit -C super a &&
	test_commit -C super b &&
	test_commit -C super/sub c &&
	set_fake_editor &&
	git -C super rebase -i HEAD^^
'

test_done
