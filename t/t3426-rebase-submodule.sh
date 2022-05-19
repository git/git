#!/bin/sh

test_description='rebase can handle submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

but_rebase () {
	but status -su >expect &&
	ls -1pR * >>expect &&
	but checkout -b ours HEAD &&
	echo x >>file1 &&
	but add file1 &&
	but cummit -m add_x &&
	but revert HEAD &&
	but status -su >actual &&
	ls -1pR * >>actual &&
	test_cmp expect actual &&
	may_only_be_test_must_fail "$2" &&
	$2 but rebase "$1"
}

test_submodule_switch_func "but_rebase"

but_rebase_interactive () {
	but status -su >expect &&
	ls -1pR * >>expect &&
	but checkout -b ours HEAD &&
	echo x >>file1 &&
	but add file1 &&
	but cummit -m add_x &&
	but revert HEAD &&
	but status -su >actual &&
	ls -1pR * >>actual &&
	test_cmp expect actual &&
	set_fake_editor &&
	echo "fake-editor.sh" >.but/info/exclude &&
	may_only_be_test_must_fail "$2" &&
	$2 but rebase -i "$1"
}

test_submodule_switch_func "but_rebase_interactive"

test_expect_success 'rebase interactive ignores modified submodules' '
	test_when_finished "rm -rf super sub" &&
	but init sub &&
	but -C sub cummit --allow-empty -m "Initial cummit" &&
	but init super &&
	but -C super submodule add ../sub &&
	but -C super config submodule.sub.ignore dirty &&
	>super/foo &&
	but -C super add foo &&
	but -C super cummit -m "Initial cummit" &&
	test_cummit -C super a &&
	test_cummit -C super b &&
	test_cummit -C super/sub c &&
	set_fake_editor &&
	but -C super rebase -i HEAD^^
'

test_done
