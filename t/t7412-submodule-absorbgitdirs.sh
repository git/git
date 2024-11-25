#!/bin/sh

test_description='Test submodule absorbgitdirs

This test verifies that `git submodue absorbgitdirs` moves a submodules git
directory into the superproject.
'

. ./test-lib.sh

test_expect_success 'setup a real submodule' '
	cwd="$(pwd)" &&
	git init sub1 &&
	test_commit -C sub1 first &&
	git submodule add ./sub1 &&
	test_tick &&
	git commit -m superproject
'

test_expect_success 'absorb the git dir' '
	>expect &&
	>actual &&
	>expect.1 &&
	>expect.2 &&
	>actual.1 &&
	>actual.2 &&
	git status >expect.1 &&
	git -C sub1 rev-parse HEAD >expect.2 &&
	cat >expect <<-EOF &&
	Migrating git directory of '\''sub1'\'' from
	'\''$cwd/sub1/.git'\'' to
	'\''$cwd/.git/modules/sub1'\''
	EOF
	git submodule absorbgitdirs 2>actual &&
	test_cmp expect actual &&
	git fsck &&
	test -f sub1/.git &&
	test -d .git/modules/sub1 &&
	git status >actual.1 &&
	git -C sub1 rev-parse HEAD >actual.2 &&
	test_cmp expect.1 actual.1 &&
	test_cmp expect.2 actual.2
'

test_expect_success 'absorbing does not fail for deinitialized submodules' '
	test_when_finished "git submodule update --init" &&
	git submodule deinit --all &&
	git submodule absorbgitdirs 2>err &&
	test_must_be_empty err &&
	test -d .git/modules/sub1 &&
	test -d sub1 &&
	! test -e sub1/.git
'

test_expect_success 'setup nested submodule' '
	git init sub1/nested &&
	test_commit -C sub1/nested first_nested &&
	git -C sub1 submodule add ./nested &&
	test_tick &&
	git -C sub1 commit -m "add nested" &&
	git add sub1 &&
	git commit -m "sub1 to include nested submodule"
'

test_expect_success 'absorb the git dir in a nested submodule' '
	git status >expect.1 &&
	git -C sub1/nested rev-parse HEAD >expect.2 &&
	cat >expect <<-EOF &&
	Migrating git directory of '\''sub1/nested'\'' from
	'\''$cwd/sub1/nested/.git'\'' to
	'\''$cwd/.git/modules/sub1/modules/nested'\''
	EOF
	git submodule absorbgitdirs 2>actual &&
	test_cmp expect actual &&
	test -f sub1/nested/.git &&
	test -d .git/modules/sub1/modules/nested &&
	git status >actual.1 &&
	git -C sub1/nested rev-parse HEAD >actual.2 &&
	test_cmp expect.1 actual.1 &&
	test_cmp expect.2 actual.2
'

test_expect_success 're-setup nested submodule' '
	# un-absorb the direct submodule, to test if the nested submodule
	# is still correct (needs a rewrite of the gitfile only)
	rm -rf sub1/.git &&
	mv .git/modules/sub1 sub1/.git &&
	GIT_WORK_TREE=. git -C sub1 config --unset core.worktree &&
	# fixup the nested submodule
	echo "gitdir: ../.git/modules/nested" >sub1/nested/.git &&
	GIT_WORK_TREE=../../../nested git -C sub1/.git/modules/nested config \
		core.worktree "../../../nested" &&
	# make sure this re-setup is correct
	git status --ignore-submodules=none &&

	# also make sure this old setup does not regress
	git submodule update --init --recursive >out 2>err &&
	test_must_be_empty out &&
	test_must_be_empty err
'

test_expect_success 'absorb the git dir in a nested submodule' '
	git status >expect.1 &&
	git -C sub1/nested rev-parse HEAD >expect.2 &&
	cat >expect <<-EOF &&
	Migrating git directory of '\''sub1'\'' from
	'\''$cwd/sub1/.git'\'' to
	'\''$cwd/.git/modules/sub1'\''
	EOF
	git submodule absorbgitdirs 2>actual &&
	test_cmp expect actual &&
	test -f sub1/.git &&
	test -f sub1/nested/.git &&
	test -d .git/modules/sub1/modules/nested &&
	git status >actual.1 &&
	git -C sub1/nested rev-parse HEAD >actual.2 &&
	test_cmp expect.1 actual.1 &&
	test_cmp expect.2 actual.2
'

test_expect_success 'absorb the git dir outside of primary worktree' '
	test_when_finished "rm -rf repo-bare.git" &&
	git clone --bare . repo-bare.git &&
	test_when_finished "rm -rf repo-wt" &&
	git -C repo-bare.git worktree add ../repo-wt &&

	test_when_finished "rm -f .gitconfig" &&
	test_config_global protocol.file.allow always &&
	git -C repo-wt submodule update --init &&
	git init repo-wt/sub2 &&
	test_commit -C repo-wt/sub2 A &&
	git -C repo-wt submodule add ./sub2 sub2 &&
	cat >expect <<-EOF &&
	Migrating git directory of '\''sub2'\'' from
	'\''$cwd/repo-wt/sub2/.git'\'' to
	'\''$cwd/repo-bare.git/worktrees/repo-wt/modules/sub2'\''
	EOF
	git -C repo-wt submodule absorbgitdirs 2>actual &&
	test_cmp expect actual
'

test_expect_success 'setup a gitlink with missing .gitmodules entry' '
	git init sub2 &&
	test_commit -C sub2 first &&
	git add sub2 &&
	git commit -m superproject
'

test_expect_success 'absorbing the git dir fails for incomplete submodules' '
	git status >expect.1 &&
	git -C sub2 rev-parse HEAD >expect.2 &&
	cat >expect <<-\EOF &&
	fatal: could not lookup name for submodule '\''sub2'\''
	EOF
	test_must_fail git submodule absorbgitdirs 2>actual &&
	test_cmp expect actual &&
	git -C sub2 fsck &&
	test -d sub2/.git &&
	git status >actual &&
	git -C sub2 rev-parse HEAD >actual.2 &&
	test_cmp expect.1 actual.1 &&
	test_cmp expect.2 actual.2
'

test_expect_success 'setup a submodule with multiple worktrees' '
	# first create another unembedded git dir in a new submodule
	git init sub3 &&
	test_commit -C sub3 first &&
	git submodule add ./sub3 &&
	test_tick &&
	git commit -m "add another submodule" &&
	git -C sub3 worktree add ../sub3_second_work_tree
'

test_expect_success 'absorbing fails for a submodule with multiple worktrees' '
	cat >expect <<-\EOF &&
	fatal: could not lookup name for submodule '\''sub2'\''
	EOF
	test_must_fail git submodule absorbgitdirs 2>actual &&
	test_cmp expect actual
'

test_done
