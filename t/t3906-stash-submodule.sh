#!/bin/sh

test_description='stash can handle submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

but_stash () {
	but status -su >expect &&
	ls -1pR * >>expect &&
	may_only_be_test_must_fail "$2" &&
	$2 but read-tree -u -m "$1" &&
	if test -n "$2"
	then
		return
	fi &&
	but stash &&
	but status -su >actual &&
	ls -1pR * >>actual &&
	test_cmp expect actual &&
	but stash apply
}

KNOWN_FAILURE_STASH_DOES_IGNORE_SUBMODULE_CHANGES=1
KNOWN_FAILURE_CHERRY_PICK_SEES_EMPTY_CUMMIT=1
KNOWN_FAILURE_NOFF_MERGE_DOESNT_CREATE_EMPTY_SUBMODULE_DIR=1
test_submodule_switch_func "but_stash"

setup_basic () {
	test_when_finished "rm -rf main sub" &&
	but init sub &&
	(
		cd sub &&
		test_cummit sub_file
	) &&
	but init main &&
	(
		cd main &&
		but submodule add ../sub &&
		test_cummit main_file
	)
}

test_expect_success 'stash push with submodule.recurse=true preserves dirty submodule worktree' '
	setup_basic &&
	(
		cd main &&
		but config submodule.recurse true &&
		echo "x" >main_file.t &&
		echo "y" >sub/sub_file.t &&
		but stash push &&
		test_must_fail but -C sub diff --quiet
	)
'

test_expect_success 'stash push and pop with submodule.recurse=true preserves dirty submodule worktree' '
	setup_basic &&
	(
		cd main &&
		but config submodule.recurse true &&
		echo "x" >main_file.t &&
		echo "y" >sub/sub_file.t &&
		but stash push &&
		but stash pop &&
		test_must_fail but -C sub diff --quiet
	)
'

test_done
