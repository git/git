#!/bin/sh

test_description='pull can handle submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

reset_branch_to_HEAD () {
	git branch -D "$1" &&
	git checkout -b "$1" HEAD &&
	git branch --set-upstream-to="origin/$1" "$1"
}

git_pull () {
	reset_branch_to_HEAD "$1" &&
	git pull
}

# pulls without conflicts
test_submodule_switch "git_pull"

git_pull_ff () {
	reset_branch_to_HEAD "$1" &&
	git pull --ff
}

test_submodule_switch "git_pull_ff"

git_pull_ff_only () {
	reset_branch_to_HEAD "$1" &&
	git pull --ff-only
}

test_submodule_switch "git_pull_ff_only"

git_pull_noff () {
	reset_branch_to_HEAD "$1" &&
	git pull --no-ff
}

KNOWN_FAILURE_NOFF_MERGE_DOESNT_CREATE_EMPTY_SUBMODULE_DIR=1
KNOWN_FAILURE_NOFF_MERGE_ATTEMPTS_TO_MERGE_REMOVED_SUBMODULE_FILES=1
test_submodule_switch "git_pull_noff"

test_expect_success 'pull --recurse-submodule setup' '
	test_create_repo child &&
	test_commit -C child bar &&

	test_create_repo parent &&
	test_commit -C child foo &&

	git -C parent submodule add ../child sub &&
	git -C parent commit -m "add submodule" &&

	git clone --recurse-submodules parent super
'

test_expect_success 'recursive pull updates working tree' '
	test_commit -C child merge_strategy &&
	git -C parent submodule update --remote &&
	git -C parent add sub &&
	git -C parent commit -m "update submodule" &&

	git -C super pull --no-rebase --recurse-submodules &&
	test_path_is_file super/sub/merge_strategy.t
'

test_expect_success 'recursive rebasing pull' '
	# change upstream
	test_commit -C child rebase_strategy &&
	git -C parent submodule update --remote &&
	git -C parent add sub &&
	git -C parent commit -m "update submodule" &&

	# also have local commits
	test_commit -C super/sub local_stuff &&

	git -C super pull --rebase --recurse-submodules &&
	test_path_is_file super/sub/rebase_strategy.t &&
	test_path_is_file super/sub/local_stuff.t
'

test_expect_success 'pull rebase recursing fails with conflicts' '

	# local changes in submodule recorded in superproject:
	test_commit -C super/sub local_stuff_2 &&
	git -C super add sub &&
	git -C super commit -m "local update submodule" &&

	# and in the remote as well:
	test_commit -C child important_upstream_work &&
	git -C parent submodule update --remote &&
	git -C parent add sub &&
	git -C parent commit -m "remote update submodule" &&

	# Unfortunately we fail here, despite no conflict in the
	# submodule itself, but the merge strategy in submodules
	# does not support rebase:
	test_must_fail git -C super pull --rebase --recurse-submodules 2>err &&
	test_i18ngrep "locally recorded submodule modifications" err
'

test_done
