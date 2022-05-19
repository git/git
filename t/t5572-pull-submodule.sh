#!/bin/sh

test_description='pull can handle submodules'

GIT_TEST_FATAL_REGISTER_SUBMODULE_ODB=1
export GIT_TEST_FATAL_REGISTER_SUBMODULE_ODB

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

reset_branch_to_HEAD () {
	but branch -D "$1" &&
	but checkout -b "$1" HEAD &&
	but branch --set-upstream-to="origin/$1" "$1"
}

but_pull () {
	reset_branch_to_HEAD "$1" &&
	may_only_be_test_must_fail "$2" &&
	$2 but pull
}

# pulls without conflicts
test_submodule_switch_func "but_pull"

but_pull_ff () {
	reset_branch_to_HEAD "$1" &&
	may_only_be_test_must_fail "$2" &&
	$2 but pull --ff
}

test_submodule_switch_func "but_pull_ff"

but_pull_ff_only () {
	reset_branch_to_HEAD "$1" &&
	may_only_be_test_must_fail "$2" &&
	$2 but pull --ff-only
}

test_submodule_switch_func "but_pull_ff_only"

but_pull_noff () {
	reset_branch_to_HEAD "$1" &&
	may_only_be_test_must_fail "$2" &&
	$2 but pull --no-ff
}

if test "$GIT_TEST_MERGE_ALGORITHM" != ort
then
	KNOWN_FAILURE_NOFF_MERGE_DOESNT_CREATE_EMPTY_SUBMODULE_DIR=1
	KNOWN_FAILURE_NOFF_MERGE_ATTEMPTS_TO_MERGE_REMOVED_SUBMODULE_FILES=1
fi
test_submodule_switch_func "but_pull_noff"

test_expect_success 'pull --recurse-submodule setup' '
	test_create_repo child &&
	test_cummit -C child bar &&

	test_create_repo parent &&
	test_cummit -C child foo &&

	but -C parent submodule add ../child sub &&
	but -C parent cummit -m "add submodule" &&

	but clone --recurse-submodules parent super
'

test_expect_success 'recursive pull updates working tree' '
	test_cummit -C child merge_strategy &&
	but -C parent submodule update --remote &&
	but -C parent add sub &&
	but -C parent cummit -m "update submodule" &&

	but -C super pull --no-rebase --recurse-submodules &&
	test_path_is_file super/sub/merge_strategy.t
'

test_expect_success "submodule.recurse option triggers recursive pull" '
	test_cummit -C child merge_strategy_2 &&
	but -C parent submodule update --remote &&
	but -C parent add sub &&
	but -C parent cummit -m "update submodule" &&

	but -C super -c submodule.recurse pull --no-rebase &&
	test_path_is_file super/sub/merge_strategy_2.t
'

test_expect_success " --[no-]recurse-submodule and submodule.recurse" '
	test_cummit -C child merge_strategy_3 &&
	but -C parent submodule update --remote &&
	but -C parent add sub &&
	but -C parent cummit -m "update submodule" &&

	but -C super -c submodule.recurse pull --no-recurse-submodules --no-rebase &&
	test_path_is_missing super/sub/merge_strategy_3.t &&
	but -C super -c submodule.recurse=false pull --recurse-submodules --no-rebase &&
	test_path_is_file super/sub/merge_strategy_3.t &&

	test_cummit -C child merge_strategy_4 &&
	but -C parent submodule update --remote &&
	but -C parent add sub &&
	but -C parent cummit -m "update submodule" &&

	but -C super -c submodule.recurse=false pull --no-recurse-submodules --no-rebase &&
	test_path_is_missing super/sub/merge_strategy_4.t &&
	but -C super -c submodule.recurse=true pull --recurse-submodules --no-rebase &&
	test_path_is_file super/sub/merge_strategy_4.t
'

test_expect_success 'pull --rebase --recurse-submodules (remote superproject submodule changes, local submodule changes)' '
	# This tests the following scenario :
	# - local submodule has new cummits
	# - local superproject does not have new cummits
	# - upstream superproject has new cummits that change the submodule pointer

	# change upstream
	test_cummit -C child rebase_strategy &&
	but -C parent submodule update --remote &&
	but -C parent add sub &&
	but -C parent cummit -m "update submodule" &&

	# also have local cummits
	test_cummit -C super/sub local_stuff &&

	but -C super pull --rebase --recurse-submodules &&
	test_path_is_file super/sub/rebase_strategy.t &&
	test_path_is_file super/sub/local_stuff.t
'

test_expect_success 'pull --rebase --recurse-submodules fails if both sides record submodule changes' '
	# This tests the following scenario :
	# - local superproject has new cummits that change the submodule pointer
	# - upstream superproject has new cummits that change the submodule pointer

	# local changes in submodule recorded in superproject:
	test_cummit -C super/sub local_stuff_2 &&
	but -C super add sub &&
	but -C super cummit -m "local update submodule" &&

	# and in the remote as well:
	test_cummit -C child important_upstream_work &&
	but -C parent submodule update --remote &&
	but -C parent add sub &&
	but -C parent cummit -m "remote update submodule" &&

	# Unfortunately we fail here, despite no conflict in the
	# submodule itself, but the merge strategy in submodules
	# does not support rebase:
	test_must_fail but -C super pull --rebase --recurse-submodules 2>err &&
	test_i18ngrep "locally recorded submodule modifications" err
'

test_expect_success 'pull --rebase --recurse-submodules (no submodule changes, no fork-point)' '
	# This tests the following scenario :
	# - local submodule does not have new cummits
	# - local superproject has new cummits that *do not* change the submodule pointer
	# - upstream superproject has new cummits that *do not* change the submodule pointer
	# - local superproject branch has no fork-point with its remote-tracking counter-part

	# create upstream superproject
	test_create_repo submodule &&
	test_cummit -C submodule first_in_sub &&

	test_create_repo superprojet &&
	test_cummit -C superprojet first_in_super &&
	but -C superprojet submodule add ../submodule &&
	but -C superprojet cummit -m "add submodule" &&
	test_cummit -C superprojet third_in_super &&

	# clone superproject
	but clone --recurse-submodules superprojet superclone &&

	# add cummits upstream
	test_cummit -C superprojet fourth_in_super &&

	# create topic branch in clone, not based on any remote-tracking branch
	but -C superclone checkout -b feat HEAD~1 &&
	test_cummit -C superclone first_on_feat &&
	but -C superclone pull --rebase --recurse-submodules origin HEAD
'

# NOTE:
#
# This test is particular because there is only a single cummit in the upstream superproject
# 'parent' (which adds the submodule 'a-submodule'). The clone of the superproject
# ('child') hard-resets its branch to a new root cummit with the same tree as the one
# from the upstream superproject, so that its branch has no merge-base with its
# remote-tracking counterpart, and then calls 'but pull --recurse-submodules --rebase'.
# The result is that the local branch is reset to the remote-tracking branch (as it was
# originally before the hard-reset).

# The only cummit in the range generated by 'submodule.c::submodule_touches_in_range' and
# passed to 'submodule.c::collect_changed_submodules' is the new (regenerated) initial cummit,
# which adds the submodule.
# However, 'submodule_touches_in_range' does not error (even though this cummit adds the submodule)
# because 'combine-diff.c::diff_tree_combined' returns early, as the initial commit has no parents.
test_expect_success 'branch has no merge base with remote-tracking counterpart' '
	rm -rf parent child &&

	test_create_repo a-submodule &&
	test_cummit -C a-submodule foo &&

	test_create_repo parent &&
	but -C parent submodule add "$(pwd)/a-submodule" &&
	but -C parent cummit -m foo &&

	but clone parent child &&

	# Reset the current branch so that it has no merge base with
	# the remote-tracking branch.
	OTHER=$(but -C child cummit-tree -m bar \
		$(but -C child rev-parse HEAD^{tree})) &&
	but -C child reset --hard "$OTHER" &&

	but -C child pull --recurse-submodules --rebase
'

test_done
