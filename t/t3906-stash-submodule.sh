#!/bin/sh

test_description='stash can handle submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

git_stash () {
	git status -su >expect &&
	ls -1pR * >>expect &&
	may_only_be_test_must_fail "$2" &&
	$2 git read-tree -u -m "$1" &&
	if test -n "$2"
	then
		return
	fi &&
	git stash &&
	git status -su >actual &&
	ls -1pR * >>actual &&
	test_cmp expect actual &&
	git stash apply
}

KNOWN_FAILURE_STASH_DOES_IGNORE_SUBMODULE_CHANGES=1
KNOWN_FAILURE_CHERRY_PICK_SEES_EMPTY_COMMIT=1
KNOWN_FAILURE_NOFF_MERGE_DOESNT_CREATE_EMPTY_SUBMODULE_DIR=1
test_submodule_switch_func "git_stash"

setup_basic () {
	test_when_finished "rm -rf main sub" &&
	git init sub &&
	(
		cd sub &&
		test_commit sub_file
	) &&
	git init main &&
	(
		cd main &&
		git submodule add ../sub &&
		test_commit main_file
	)
}

test_expect_success 'stash push with submodule.recurse=true preserves dirty submodule worktree' '
	setup_basic &&
	(
		cd main &&
		git config submodule.recurse true &&
		echo "x" >main_file.t &&
		echo "y" >sub/sub_file.t &&
		git stash push &&
		test_must_fail git -C sub diff --quiet
	)
'

test_expect_success 'stash push and pop with submodule.recurse=true preserves dirty submodule worktree' '
	setup_basic &&
	(
		cd main &&
		git config submodule.recurse true &&
		echo "x" >main_file.t &&
		echo "y" >sub/sub_file.t &&
		git stash push &&
		git stash pop &&
		test_must_fail git -C sub diff --quiet
	)
'

test_done
