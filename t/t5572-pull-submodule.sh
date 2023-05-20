#!/bin/sh

test_description='pull can handle submodules'

GIT_TEST_FATAL_REGISTER_SUBMODULE_ODB=1
export GIT_TEST_FATAL_REGISTER_SUBMODULE_ODB

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

reset_branch_to_HEAD () {
	git branch -D "$1" &&
	git checkout -b "$1" HEAD &&
	git branch --set-upstream-to="origin/$1" "$1"
}

git_pull () {
	reset_branch_to_HEAD "$1" &&
	may_only_be_test_must_fail "$2" &&
	$2 git pull
}

# pulls without conflicts
test_submodule_switch_func "git_pull"

git_pull_ff () {
	reset_branch_to_HEAD "$1" &&
	may_only_be_test_must_fail "$2" &&
	$2 git pull --ff
}

test_submodule_switch_func "git_pull_ff"

git_pull_ff_only () {
	reset_branch_to_HEAD "$1" &&
	may_only_be_test_must_fail "$2" &&
	$2 git pull --ff-only
}

test_submodule_switch_func "git_pull_ff_only"

git_pull_noff () {
	reset_branch_to_HEAD "$1" &&
	may_only_be_test_must_fail "$2" &&
	$2 git pull --no-ff
}

if test "$GIT_TEST_MERGE_ALGORITHM" != ort
then
	KNOWN_FAILURE_NOFF_MERGE_DOESNT_CREATE_EMPTY_SUBMODULE_DIR=1
	KNOWN_FAILURE_NOFF_MERGE_ATTEMPTS_TO_MERGE_REMOVED_SUBMODULE_FILES=1
fi
test_submodule_switch_func "git_pull_noff"

test_expect_success 'setup' '
	git config --global protocol.file.allow always
'

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

test_expect_success "submodule.recurse option triggers recursive pull" '
	test_commit -C child merge_strategy_2 &&
	git -C parent submodule update --remote &&
	git -C parent add sub &&
	git -C parent commit -m "update submodule" &&

	git -C super -c submodule.recurse pull --no-rebase &&
	test_path_is_file super/sub/merge_strategy_2.t
'

test_expect_success " --[no-]recurse-submodule and submodule.recurse" '
	test_commit -C child merge_strategy_3 &&
	git -C parent submodule update --remote &&
	git -C parent add sub &&
	git -C parent commit -m "update submodule" &&

	git -C super -c submodule.recurse pull --no-recurse-submodules --no-rebase &&
	test_path_is_missing super/sub/merge_strategy_3.t &&
	git -C super -c submodule.recurse=false pull --recurse-submodules --no-rebase &&
	test_path_is_file super/sub/merge_strategy_3.t &&

	test_commit -C child merge_strategy_4 &&
	git -C parent submodule update --remote &&
	git -C parent add sub &&
	git -C parent commit -m "update submodule" &&

	git -C super -c submodule.recurse=false pull --no-recurse-submodules --no-rebase &&
	test_path_is_missing super/sub/merge_strategy_4.t &&
	git -C super -c submodule.recurse=true pull --recurse-submodules --no-rebase &&
	test_path_is_file super/sub/merge_strategy_4.t
'

test_expect_success "fetch.recurseSubmodules option triggers recursive fetch (but not recursive update)" '
	test_commit -C child merge_strategy_5 &&
	# Omit the parent commit, otherwise this passes with the
	# default "pull" behavior.

	git -C super -c fetch.recursesubmodules=true pull --no-rebase &&
	# Check that the submodule commit was fetched
	sub_oid=$(git -C child rev-parse HEAD) &&
	git -C super/sub cat-file -e $sub_oid &&
	# Check that the submodule worktree did not update
	test_path_is_missing super/sub/merge_strategy_5.t
'

test_expect_success "fetch.recurseSubmodules takes precedence over submodule.recurse" '
	test_commit -C child merge_strategy_6 &&
	# Omit the parent commit, otherwise this passes with the
	# default "pull" behavior.

	git -C super -c submodule.recurse=false -c fetch.recursesubmodules=true pull --no-rebase &&
	# Check that the submodule commit was fetched
	sub_oid=$(git -C child rev-parse HEAD) &&
	git -C super/sub cat-file -e $sub_oid &&
	# Check that the submodule worktree did not update
	test_path_is_missing super/sub/merge_strategy_6.t
'

test_expect_success 'pull --rebase --recurse-submodules (remote superproject submodule changes, local submodule changes)' '
	# This tests the following scenario :
	# - local submodule has new commits
	# - local superproject does not have new commits
	# - upstream superproject has new commits that change the submodule pointer

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

test_expect_success 'pull --rebase --recurse-submodules fails if both sides record submodule changes' '
	# This tests the following scenario :
	# - local superproject has new commits that change the submodule pointer
	# - upstream superproject has new commits that change the submodule pointer

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

test_expect_success 'pull --rebase --recurse-submodules (no submodule changes, no fork-point)' '
	# This tests the following scenario :
	# - local submodule does not have new commits
	# - local superproject has new commits that *do not* change the submodule pointer
	# - upstream superproject has new commits that *do not* change the submodule pointer
	# - local superproject branch has no fork-point with its remote-tracking counter-part

	# create upstream superproject
	test_create_repo submodule &&
	test_commit -C submodule first_in_sub &&

	test_create_repo superprojet &&
	test_commit -C superprojet first_in_super &&
	git -C superprojet submodule add ../submodule &&
	git -C superprojet commit -m "add submodule" &&
	test_commit -C superprojet third_in_super &&

	# clone superproject
	git clone --recurse-submodules superprojet superclone &&

	# add commits upstream
	test_commit -C superprojet fourth_in_super &&

	# create topic branch in clone, not based on any remote-tracking branch
	git -C superclone checkout -b feat HEAD~1 &&
	test_commit -C superclone first_on_feat &&
	git -C superclone pull --rebase --recurse-submodules origin HEAD
'

# NOTE:
#
# This test is particular because there is only a single commit in the upstream superproject
# 'parent' (which adds the submodule 'a-submodule'). The clone of the superproject
# ('child') hard-resets its branch to a new root commit with the same tree as the one
# from the upstream superproject, so that its branch has no merge-base with its
# remote-tracking counterpart, and then calls 'git pull --recurse-submodules --rebase'.
# The result is that the local branch is reset to the remote-tracking branch (as it was
# originally before the hard-reset).

# The only commit in the range generated by 'submodule.c::submodule_touches_in_range' and
# passed to 'submodule.c::collect_changed_submodules' is the new (regenerated) initial commit,
# which adds the submodule.
# However, 'submodule_touches_in_range' does not error (even though this commit adds the submodule)
# because 'combine-diff.c::diff_tree_combined' returns early, as the initial commit has no parents.
test_expect_success 'branch has no merge base with remote-tracking counterpart' '
	rm -rf parent child &&

	test_create_repo a-submodule &&
	test_commit -C a-submodule foo &&

	test_create_repo parent &&
	git -C parent submodule add "$(pwd)/a-submodule" &&
	git -C parent commit -m foo &&

	git clone parent child &&

	# Reset the current branch so that it has no merge base with
	# the remote-tracking branch.
	OTHER=$(git -C child commit-tree -m bar \
		$(git -C child rev-parse HEAD^{tree})) &&
	git -C child reset --hard "$OTHER" &&

	git -C child pull --recurse-submodules --rebase
'

test_done
