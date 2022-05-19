#!/bin/sh

test_description='Test submodule absorbbutdirs

This test verifies that `but submodue absorbbutdirs` moves a submodules but
directory into the superproject.
'

. ./test-lib.sh

test_expect_success 'setup a real submodule' '
	but init sub1 &&
	test_cummit -C sub1 first &&
	but submodule add ./sub1 &&
	test_tick &&
	but cummit -m superproject
'

test_expect_success 'absorb the but dir' '
	>expect.1 &&
	>expect.2 &&
	>actual.1 &&
	>actual.2 &&
	but status >expect.1 &&
	but -C sub1 rev-parse HEAD >expect.2 &&
	but submodule absorbbutdirs &&
	but fsck &&
	test -f sub1/.but &&
	test -d .but/modules/sub1 &&
	but status >actual.1 &&
	but -C sub1 rev-parse HEAD >actual.2 &&
	test_cmp expect.1 actual.1 &&
	test_cmp expect.2 actual.2
'

test_expect_success 'absorbing does not fail for deinitialized submodules' '
	test_when_finished "but submodule update --init" &&
	but submodule deinit --all &&
	but submodule absorbbutdirs &&
	test -d .but/modules/sub1 &&
	test -d sub1 &&
	! test -e sub1/.but
'

test_expect_success 'setup nested submodule' '
	but init sub1/nested &&
	test_cummit -C sub1/nested first_nested &&
	but -C sub1 submodule add ./nested &&
	test_tick &&
	but -C sub1 cummit -m "add nested" &&
	but add sub1 &&
	but cummit -m "sub1 to include nested submodule"
'

test_expect_success 'absorb the but dir in a nested submodule' '
	but status >expect.1 &&
	but -C sub1/nested rev-parse HEAD >expect.2 &&
	but submodule absorbbutdirs &&
	test -f sub1/nested/.but &&
	test -d .but/modules/sub1/modules/nested &&
	but status >actual.1 &&
	but -C sub1/nested rev-parse HEAD >actual.2 &&
	test_cmp expect.1 actual.1 &&
	test_cmp expect.2 actual.2
'

test_expect_success 're-setup nested submodule' '
	# un-absorb the direct submodule, to test if the nested submodule
	# is still correct (needs a rewrite of the butfile only)
	rm -rf sub1/.but &&
	mv .but/modules/sub1 sub1/.but &&
	GIT_WORK_TREE=. but -C sub1 config --unset core.worktree &&
	# fixup the nested submodule
	echo "butdir: ../.but/modules/nested" >sub1/nested/.but &&
	GIT_WORK_TREE=../../../nested but -C sub1/.but/modules/nested config \
		core.worktree "../../../nested" &&
	# make sure this re-setup is correct
	but status --ignore-submodules=none &&

	# also make sure this old setup does not regress
	but submodule update --init --recursive >out 2>err &&
	test_must_be_empty out &&
	test_must_be_empty err
'

test_expect_success 'absorb the but dir in a nested submodule' '
	but status >expect.1 &&
	but -C sub1/nested rev-parse HEAD >expect.2 &&
	but submodule absorbbutdirs &&
	test -f sub1/.but &&
	test -f sub1/nested/.but &&
	test -d .but/modules/sub1/modules/nested &&
	but status >actual.1 &&
	but -C sub1/nested rev-parse HEAD >actual.2 &&
	test_cmp expect.1 actual.1 &&
	test_cmp expect.2 actual.2
'

test_expect_success 'setup a butlink with missing .butmodules entry' '
	but init sub2 &&
	test_cummit -C sub2 first &&
	but add sub2 &&
	but cummit -m superproject
'

test_expect_success 'absorbing the but dir fails for incomplete submodules' '
	but status >expect.1 &&
	but -C sub2 rev-parse HEAD >expect.2 &&
	test_must_fail but submodule absorbbutdirs &&
	but -C sub2 fsck &&
	test -d sub2/.but &&
	but status >actual &&
	but -C sub2 rev-parse HEAD >actual.2 &&
	test_cmp expect.1 actual.1 &&
	test_cmp expect.2 actual.2
'

test_expect_success 'setup a submodule with multiple worktrees' '
	# first create another unembedded but dir in a new submodule
	but init sub3 &&
	test_cummit -C sub3 first &&
	but submodule add ./sub3 &&
	test_tick &&
	but cummit -m "add another submodule" &&
	but -C sub3 worktree add ../sub3_second_work_tree
'

test_expect_success 'absorbing fails for a submodule with multiple worktrees' '
	test_must_fail but submodule absorbbutdirs sub3 2>error &&
	test_i18ngrep "not supported" error
'

test_done
