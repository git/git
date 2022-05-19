#!/bin/sh

test_description='test but worktree add'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

test_expect_success 'setup' '
	test_cummit init
'

test_expect_success '"add" an existing worktree' '
	mkdir -p existing/subtree &&
	test_must_fail but worktree add --detach existing main
'

test_expect_success '"add" an existing empty worktree' '
	mkdir existing_empty &&
	but worktree add --detach existing_empty main
'

test_expect_success '"add" using shorthand - fails when no previous branch' '
	test_must_fail but worktree add existing_short -
'

test_expect_success '"add" using - shorthand' '
	but checkout -b newbranch &&
	echo hello >myworld &&
	but add myworld &&
	but cummit -m myworld &&
	but checkout main &&
	but worktree add short-hand - &&
	echo refs/heads/newbranch >expect &&
	but -C short-hand rev-parse --symbolic-full-name HEAD >actual &&
	test_cmp expect actual
'

test_expect_success '"add" refuses to checkout locked branch' '
	test_must_fail but worktree add zere main &&
	! test -d zere &&
	! test -d .but/worktrees/zere
'

test_expect_success 'checking out paths not complaining about linked checkouts' '
	(
	cd existing_empty &&
	echo dirty >>init.t &&
	but checkout main -- init.t
	)
'

test_expect_success '"add" worktree' '
	but rev-parse HEAD >expect &&
	but worktree add --detach here main &&
	(
		cd here &&
		test_cmp ../init.t init.t &&
		test_must_fail but symbolic-ref HEAD &&
		but rev-parse HEAD >actual &&
		test_cmp ../expect actual &&
		but fsck
	)
'

test_expect_success '"add" worktree with lock' '
	but worktree add --detach --lock here-with-lock main &&
	test_when_finished "but worktree unlock here-with-lock || :" &&
	test -f .but/worktrees/here-with-lock/locked
'

test_expect_success '"add" worktree with lock and reason' '
	lock_reason="why not" &&
	but worktree add --detach --lock --reason "$lock_reason" here-with-lock-reason main &&
	test_when_finished "but worktree unlock here-with-lock-reason || :" &&
	test -f .but/worktrees/here-with-lock-reason/locked &&
	echo "$lock_reason" >expect &&
	test_cmp expect .but/worktrees/here-with-lock-reason/locked
'

test_expect_success '"add" worktree with reason but no lock' '
	test_must_fail but worktree add --detach --reason "why not" here-with-reason-only main &&
	test_path_is_missing .but/worktrees/here-with-reason-only/locked
'

test_expect_success '"add" worktree from a subdir' '
	(
		mkdir sub &&
		cd sub &&
		but worktree add --detach here main &&
		cd here &&
		test_cmp ../../init.t init.t
	)
'

test_expect_success '"add" from a linked checkout' '
	(
		cd here &&
		but worktree add --detach nested-here main &&
		cd nested-here &&
		but fsck
	)
'

test_expect_success '"add" worktree creating new branch' '
	but worktree add -b newmain there main &&
	(
		cd there &&
		test_cmp ../init.t init.t &&
		but symbolic-ref HEAD >actual &&
		echo refs/heads/newmain >expect &&
		test_cmp expect actual &&
		but fsck
	)
'

test_expect_success 'die the same branch is already checked out' '
	(
		cd here &&
		test_must_fail but checkout newmain
	)
'

test_expect_success SYMLINKS 'die the same branch is already checked out (symlink)' '
	head=$(but -C there rev-parse --but-path HEAD) &&
	ref=$(but -C there symbolic-ref HEAD) &&
	rm "$head" &&
	ln -s "$ref" "$head" &&
	test_must_fail but -C here checkout newmain
'

test_expect_success 'not die the same branch is already checked out' '
	(
		cd here &&
		but worktree add --force anothernewmain newmain
	)
'

test_expect_success 'not die on re-checking out current branch' '
	(
		cd there &&
		but checkout newmain
	)
'

test_expect_success '"add" from a bare repo' '
	(
		but clone --bare . bare &&
		cd bare &&
		but worktree add -b bare-main ../there2 main
	)
'

test_expect_success 'checkout from a bare repo without "add"' '
	(
		cd bare &&
		test_must_fail but checkout main
	)
'

test_expect_success '"add" default branch of a bare repo' '
	(
		but clone --bare . bare2 &&
		cd bare2 &&
		but worktree add ../there3 main &&
		cd ../there3 &&
		# Simple check that a Git command does not
		# immediately fail with the current setup
		but status
	) &&
	cat >expect <<-EOF &&
	init.t
	EOF
	ls there3 >actual &&
	test_cmp expect actual
'

test_expect_success '"add" to bare repo with worktree config' '
	(
		but clone --bare . bare3 &&
		cd bare3 &&
		but config extensions.worktreeconfig true &&

		# Add config values that are erroneous to have in
		# a config.worktree file outside of the main
		# working tree, to check that Git filters them out
		# when copying config during "but worktree add".
		but config --worktree core.bare true &&
		but config --worktree core.worktree "$(pwd)" &&

		# We want to check that bogus.key is copied
		but config --worktree bogus.key value &&
		but config --unset core.bare &&
		but worktree add ../there4 main &&
		cd ../there4 &&

		# Simple check that a Git command does not
		# immediately fail with the current setup
		but status &&
		but worktree add --detach ../there5 &&
		cd ../there5 &&
		but status
	) &&

	# the worktree has the arbitrary value copied.
	test_cmp_config -C there4 value bogus.key &&
	test_cmp_config -C there5 value bogus.key &&

	# however, core.bare and core.worktree were removed.
	test_must_fail but -C there4 config core.bare &&
	test_must_fail but -C there4 config core.worktree &&

	cat >expect <<-EOF &&
	init.t
	EOF

	ls there4 >actual &&
	test_cmp expect actual &&
	ls there5 >actual &&
	test_cmp expect actual
'

test_expect_success 'checkout with grafts' '
	test_when_finished rm .but/info/grafts &&
	test_cummit abc &&
	SHA1=$(but rev-parse HEAD) &&
	test_cummit def &&
	test_cummit xyz &&
	echo "$(but rev-parse HEAD) $SHA1" >.but/info/grafts &&
	cat >expected <<-\EOF &&
	xyz
	abc
	EOF
	but log --format=%s -2 >actual &&
	test_cmp expected actual &&
	but worktree add --detach grafted main &&
	but --but-dir=grafted/.but log --format=%s -2 >actual &&
	test_cmp expected actual
'

test_expect_success '"add" from relative HEAD' '
	test_cummit a &&
	test_cummit b &&
	test_cummit c &&
	but rev-parse HEAD~1 >expected &&
	but worktree add relhead HEAD~1 &&
	but -C relhead rev-parse HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '"add -b" with <branch> omitted' '
	but worktree add -b burble flornk &&
	test_cmp_rev HEAD burble
'

test_expect_success '"add --detach" with <branch> omitted' '
	but worktree add --detach fishhook &&
	but rev-parse HEAD >expected &&
	but -C fishhook rev-parse HEAD >actual &&
	test_cmp expected actual &&
	test_must_fail but -C fishhook symbolic-ref HEAD
'

test_expect_success '"add" with <branch> omitted' '
	but worktree add wiffle/bat &&
	test_cmp_rev HEAD bat
'

test_expect_success '"add" checks out existing branch of dwimd name' '
	but branch dwim HEAD~1 &&
	but worktree add dwim &&
	test_cmp_rev HEAD~1 dwim &&
	(
		cd dwim &&
		test_cmp_rev HEAD dwim
	)
'

test_expect_success '"add <path>" dwim fails with checked out branch' '
	but checkout -b test-branch &&
	test_must_fail but worktree add test-branch &&
	test_path_is_missing test-branch
'

test_expect_success '"add --force" with existing dwimd name doesnt die' '
	but checkout test-branch &&
	but worktree add --force test-branch
'

test_expect_success '"add" no auto-vivify with --detach and <branch> omitted' '
	but worktree add --detach mish/mash &&
	test_must_fail but rev-parse mash -- &&
	test_must_fail but -C mish/mash symbolic-ref HEAD
'

test_expect_success '"add" -b/-B mutually exclusive' '
	test_must_fail but worktree add -b poodle -B poodle bamboo main
'

test_expect_success '"add" -b/--detach mutually exclusive' '
	test_must_fail but worktree add -b poodle --detach bamboo main
'

test_expect_success '"add" -B/--detach mutually exclusive' '
	test_must_fail but worktree add -B poodle --detach bamboo main
'

test_expect_success '"add -B" fails if the branch is checked out' '
	but rev-parse newmain >before &&
	test_must_fail but worktree add -B newmain bamboo main &&
	but rev-parse newmain >after &&
	test_cmp before after
'

test_expect_success 'add -B' '
	but worktree add -B poodle bamboo2 main^ &&
	but -C bamboo2 symbolic-ref HEAD >actual &&
	echo refs/heads/poodle >expected &&
	test_cmp expected actual &&
	test_cmp_rev main^ poodle
'

test_expect_success 'add --quiet' '
	but worktree add --quiet another-worktree main 2>actual &&
	test_must_be_empty actual
'

test_expect_success 'local clone from linked checkout' '
	but clone --local here here-clone &&
	( cd here-clone && but fsck )
'

test_expect_success 'local clone --shared from linked checkout' '
	but -C bare worktree add --detach ../baretree &&
	but clone --local --shared baretree bare-clone &&
	grep /bare/ bare-clone/.but/objects/info/alternates
'

test_expect_success '"add" worktree with --no-checkout' '
	but worktree add --no-checkout -b swamp swamp &&
	! test -e swamp/init.t &&
	but -C swamp reset --hard &&
	test_cmp init.t swamp/init.t
'

test_expect_success '"add" worktree with --checkout' '
	but worktree add --checkout -b swmap2 swamp2 &&
	test_cmp init.t swamp2/init.t
'

test_expect_success 'put a worktree under rebase' '
	but worktree add under-rebase &&
	(
		cd under-rebase &&
		set_fake_editor &&
		FAKE_LINES="edit 1" but rebase -i HEAD^ &&
		but worktree list | grep "under-rebase.*detached HEAD"
	)
'

test_expect_success 'add a worktree, checking out a rebased branch' '
	test_must_fail but worktree add new-rebase under-rebase &&
	! test -d new-rebase
'

test_expect_success 'checking out a rebased branch from another worktree' '
	but worktree add new-place &&
	test_must_fail but -C new-place checkout under-rebase
'

test_expect_success 'not allow to delete a branch under rebase' '
	(
		cd under-rebase &&
		test_must_fail but branch -D under-rebase
	)
'

test_expect_success 'rename a branch under rebase not allowed' '
	test_must_fail but branch -M under-rebase rebase-with-new-name
'

test_expect_success 'check out from current worktree branch ok' '
	(
		cd under-rebase &&
		but checkout under-rebase &&
		but checkout - &&
		but rebase --abort
	)
'

test_expect_success 'checkout a branch under bisect' '
	but worktree add under-bisect &&
	(
		cd under-bisect &&
		but bisect start &&
		but bisect bad &&
		but bisect good HEAD~2 &&
		but worktree list | grep "under-bisect.*detached HEAD" &&
		test_must_fail but worktree add new-bisect under-bisect &&
		! test -d new-bisect
	)
'

test_expect_success 'rename a branch under bisect not allowed' '
	test_must_fail but branch -M under-bisect bisect-with-new-name
'
# Is branch "refs/heads/$1" set to pull from "$2/$3"?
test_branch_upstream () {
	printf "%s\n" "$2" "refs/heads/$3" >expect.upstream &&
	{
		but config "branch.$1.remote" &&
		but config "branch.$1.merge"
	} >actual.upstream &&
	test_cmp expect.upstream actual.upstream
}

test_expect_success '--track sets up tracking' '
	test_when_finished rm -rf track &&
	but worktree add --track -b track track main &&
	test_branch_upstream track . main
'

# setup remote repository $1 and repository $2 with $1 set up as
# remote.  The remote has two branches, main and foo.
setup_remote_repo () {
	but init $1 &&
	(
		cd $1 &&
		test_cummit $1_main &&
		but checkout -b foo &&
		test_cummit upstream_foo
	) &&
	but init $2 &&
	(
		cd $2 &&
		test_cummit $2_main &&
		but remote add $1 ../$1 &&
		but config remote.$1.fetch \
			"refs/heads/*:refs/remotes/$1/*" &&
		but fetch --all
	)
}

test_expect_success '--no-track avoids setting up tracking' '
	test_when_finished rm -rf repo_upstream repo_local foo &&
	setup_remote_repo repo_upstream repo_local &&
	(
		cd repo_local &&
		but worktree add --no-track -b foo ../foo repo_upstream/foo
	) &&
	(
		cd foo &&
		test_must_fail but config "branch.foo.remote" &&
		test_must_fail but config "branch.foo.merge" &&
		test_cmp_rev refs/remotes/repo_upstream/foo refs/heads/foo
	)
'

test_expect_success '"add" <path> <non-existent-branch> fails' '
	test_must_fail but worktree add foo non-existent
'

test_expect_success '"add" <path> <branch> dwims' '
	test_when_finished rm -rf repo_upstream repo_dwim foo &&
	setup_remote_repo repo_upstream repo_dwim &&
	but init repo_dwim &&
	(
		cd repo_dwim &&
		but worktree add ../foo foo
	) &&
	(
		cd foo &&
		test_branch_upstream foo repo_upstream foo &&
		test_cmp_rev refs/remotes/repo_upstream/foo refs/heads/foo
	)
'

test_expect_success '"add" <path> <branch> dwims with checkout.defaultRemote' '
	test_when_finished rm -rf repo_upstream repo_dwim foo &&
	setup_remote_repo repo_upstream repo_dwim &&
	but init repo_dwim &&
	(
		cd repo_dwim &&
		but remote add repo_upstream2 ../repo_upstream &&
		but fetch repo_upstream2 &&
		test_must_fail but worktree add ../foo foo &&
		but -c checkout.defaultRemote=repo_upstream worktree add ../foo foo &&
		but status -uno --porcelain >status.actual &&
		test_must_be_empty status.actual
	) &&
	(
		cd foo &&
		test_branch_upstream foo repo_upstream foo &&
		test_cmp_rev refs/remotes/repo_upstream/foo refs/heads/foo
	)
'

test_expect_success 'but worktree add does not match remote' '
	test_when_finished rm -rf repo_a repo_b foo &&
	setup_remote_repo repo_a repo_b &&
	(
		cd repo_b &&
		but worktree add ../foo
	) &&
	(
		cd foo &&
		test_must_fail but config "branch.foo.remote" &&
		test_must_fail but config "branch.foo.merge" &&
		test_cmp_rev ! refs/remotes/repo_a/foo refs/heads/foo
	)
'

test_expect_success 'but worktree add --guess-remote sets up tracking' '
	test_when_finished rm -rf repo_a repo_b foo &&
	setup_remote_repo repo_a repo_b &&
	(
		cd repo_b &&
		but worktree add --guess-remote ../foo
	) &&
	(
		cd foo &&
		test_branch_upstream foo repo_a foo &&
		test_cmp_rev refs/remotes/repo_a/foo refs/heads/foo
	)
'

test_expect_success 'but worktree add with worktree.guessRemote sets up tracking' '
	test_when_finished rm -rf repo_a repo_b foo &&
	setup_remote_repo repo_a repo_b &&
	(
		cd repo_b &&
		but config worktree.guessRemote true &&
		but worktree add ../foo
	) &&
	(
		cd foo &&
		test_branch_upstream foo repo_a foo &&
		test_cmp_rev refs/remotes/repo_a/foo refs/heads/foo
	)
'

test_expect_success 'but worktree --no-guess-remote option overrides config' '
	test_when_finished rm -rf repo_a repo_b foo &&
	setup_remote_repo repo_a repo_b &&
	(
		cd repo_b &&
		but config worktree.guessRemote true &&
		but worktree add --no-guess-remote ../foo
	) &&
	(
		cd foo &&
		test_must_fail but config "branch.foo.remote" &&
		test_must_fail but config "branch.foo.merge" &&
		test_cmp_rev ! refs/remotes/repo_a/foo refs/heads/foo
	)
'

post_checkout_hook () {
	test_hook -C "$1" post-checkout <<-\EOF
	{
		echo $*
		but rev-parse --but-dir --show-toplevel
	} >hook.actual
	EOF
}

test_expect_success '"add" invokes post-checkout hook (branch)' '
	post_checkout_hook &&
	{
		echo $ZERO_OID $(but rev-parse HEAD) 1 &&
		echo $(pwd)/.but/worktrees/gumby &&
		echo $(pwd)/gumby
	} >hook.expect &&
	but worktree add gumby &&
	test_cmp hook.expect gumby/hook.actual
'

test_expect_success '"add" invokes post-checkout hook (detached)' '
	post_checkout_hook &&
	{
		echo $ZERO_OID $(but rev-parse HEAD) 1 &&
		echo $(pwd)/.but/worktrees/grumpy &&
		echo $(pwd)/grumpy
	} >hook.expect &&
	but worktree add --detach grumpy &&
	test_cmp hook.expect grumpy/hook.actual
'

test_expect_success '"add --no-checkout" suppresses post-checkout hook' '
	post_checkout_hook &&
	rm -f hook.actual &&
	but worktree add --no-checkout gloopy &&
	test_path_is_missing gloopy/hook.actual
'

test_expect_success '"add" in other worktree invokes post-checkout hook' '
	post_checkout_hook &&
	{
		echo $ZERO_OID $(but rev-parse HEAD) 1 &&
		echo $(pwd)/.but/worktrees/guppy &&
		echo $(pwd)/guppy
	} >hook.expect &&
	but -C gloopy worktree add --detach ../guppy &&
	test_cmp hook.expect guppy/hook.actual
'

test_expect_success '"add" in bare repo invokes post-checkout hook' '
	rm -rf bare &&
	but clone --bare . bare &&
	{
		echo $ZERO_OID $(but --but-dir=bare rev-parse HEAD) 1 &&
		echo $(pwd)/bare/worktrees/goozy &&
		echo $(pwd)/goozy
	} >hook.expect &&
	post_checkout_hook bare &&
	but -C bare worktree add --detach ../goozy &&
	test_cmp hook.expect goozy/hook.actual
'

test_expect_success '"add" an existing but missing worktree' '
	but worktree add --detach pneu &&
	test_must_fail but worktree add --detach pneu &&
	rm -fr pneu &&
	test_must_fail but worktree add --detach pneu &&
	but worktree add --force --detach pneu
'

test_expect_success '"add" an existing locked but missing worktree' '
	but worktree add --detach gnoo &&
	but worktree lock gnoo &&
	test_when_finished "but worktree unlock gnoo || :" &&
	rm -fr gnoo &&
	test_must_fail but worktree add --detach gnoo &&
	test_must_fail but worktree add --force --detach gnoo &&
	but worktree add --force --force --detach gnoo
'

test_expect_success '"add" not tripped up by magic worktree matching"' '
	# if worktree "sub1/bar" exists, "but worktree add bar" in distinct
	# directory `sub2` should not mistakenly complain that `bar` is an
	# already-registered worktree
	mkdir sub1 sub2 &&
	but -C sub1 --but-dir=../.but worktree add --detach bozo &&
	but -C sub2 --but-dir=../.but worktree add --detach bozo
'

test_expect_success FUNNYNAMES 'sanitize generated worktree name' '
	but worktree add --detach ".  weird*..?.lock.lock" &&
	test -d .but/worktrees/---weird-.-
'

test_expect_success '"add" should not fail because of another bad worktree' '
	but init add-fail &&
	(
		cd add-fail &&
		test_cummit first &&
		mkdir sub &&
		but worktree add sub/to-be-deleted &&
		rm -rf sub &&
		but worktree add second
	)
'

test_expect_success '"add" with uninitialized submodule, with submodule.recurse unset' '
	test_create_repo submodule &&
	test_cummit -C submodule first &&
	test_create_repo project &&
	but -C project submodule add ../submodule &&
	but -C project add submodule &&
	test_tick &&
	but -C project cummit -m add_sub &&
	but clone project project-clone &&
	but -C project-clone worktree add ../project-2
'
test_expect_success '"add" with uninitialized submodule, with submodule.recurse set' '
	but -C project-clone -c submodule.recurse worktree add ../project-3
'

test_expect_success '"add" with initialized submodule, with submodule.recurse unset' '
	but -C project-clone submodule update --init &&
	but -C project-clone worktree add ../project-4
'

test_expect_success '"add" with initialized submodule, with submodule.recurse set' '
	but -C project-clone -c submodule.recurse worktree add ../project-5
'

test_done
