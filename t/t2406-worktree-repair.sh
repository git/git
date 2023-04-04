#!/bin/sh

test_description='test git worktree repair'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	test_commit init
'

test_expect_success 'skip missing worktree' '
	test_when_finished "git worktree prune" &&
	git worktree add --detach missing &&
	rm -rf missing &&
	git worktree repair >out 2>err &&
	test_must_be_empty out &&
	test_must_be_empty err
'

test_expect_success 'worktree path not directory' '
	test_when_finished "git worktree prune" &&
	git worktree add --detach notdir &&
	rm -rf notdir &&
	>notdir &&
	test_must_fail git worktree repair >out 2>err &&
	test_must_be_empty out &&
	test_i18ngrep "not a directory" err
'

test_expect_success "don't clobber .git repo" '
	test_when_finished "rm -rf repo && git worktree prune" &&
	git worktree add --detach repo &&
	rm -rf repo &&
	test_create_repo repo &&
	test_must_fail git worktree repair >out 2>err &&
	test_must_be_empty out &&
	test_i18ngrep ".git is not a file" err
'

test_corrupt_gitfile () {
	butcher=$1 &&
	problem=$2 &&
	repairdir=${3:-.} &&
	test_when_finished 'rm -rf corrupt && git worktree prune' &&
	git worktree add --detach corrupt &&
	git -C corrupt rev-parse --absolute-git-dir >expect &&
	eval "$butcher" &&
	git -C "$repairdir" worktree repair 2>err &&
	test_i18ngrep "$problem" err &&
	git -C corrupt rev-parse --absolute-git-dir >actual &&
	test_cmp expect actual
}

test_expect_success 'repair missing .git file' '
	test_corrupt_gitfile "rm -f corrupt/.git" ".git file broken"
'

test_expect_success 'repair bogus .git file' '
	test_corrupt_gitfile "echo \"gitdir: /nowhere\" >corrupt/.git" \
		".git file broken"
'

test_expect_success 'repair incorrect .git file' '
	test_when_finished "rm -rf other && git worktree prune" &&
	test_create_repo other &&
	other=$(git -C other rev-parse --absolute-git-dir) &&
	test_corrupt_gitfile "echo \"gitdir: $other\" >corrupt/.git" \
		".git file incorrect"
'

test_expect_success 'repair .git file from main/.git' '
	test_corrupt_gitfile "rm -f corrupt/.git" ".git file broken" .git
'

test_expect_success 'repair .git file from linked worktree' '
	test_when_finished "rm -rf other && git worktree prune" &&
	git worktree add --detach other &&
	test_corrupt_gitfile "rm -f corrupt/.git" ".git file broken" other
'

test_expect_success 'repair .git file from bare.git' '
	test_when_finished "rm -rf bare.git corrupt && git worktree prune" &&
	git clone --bare . bare.git &&
	git -C bare.git worktree add --detach ../corrupt &&
	git -C corrupt rev-parse --absolute-git-dir >expect &&
	rm -f corrupt/.git &&
	git -C bare.git worktree repair &&
	git -C corrupt rev-parse --absolute-git-dir >actual &&
	test_cmp expect actual
'

test_expect_success 'invalid worktree path' '
	test_must_fail git worktree repair /notvalid >out 2>err &&
	test_must_be_empty out &&
	test_i18ngrep "not a valid path" err
'

test_expect_success 'repo not found; .git not file' '
	test_when_finished "rm -rf not-a-worktree" &&
	test_create_repo not-a-worktree &&
	test_must_fail git worktree repair not-a-worktree >out 2>err &&
	test_must_be_empty out &&
	test_i18ngrep ".git is not a file" err
'

test_expect_success 'repo not found; .git not referencing repo' '
	test_when_finished "rm -rf side not-a-repo && git worktree prune" &&
	git worktree add --detach side &&
	sed s,\.git/worktrees/side$,not-a-repo, side/.git >side/.newgit &&
	mv side/.newgit side/.git &&
	mkdir not-a-repo &&
	test_must_fail git worktree repair side 2>err &&
	test_i18ngrep ".git file does not reference a repository" err
'

test_expect_success 'repo not found; .git file broken' '
	test_when_finished "rm -rf orig moved && git worktree prune" &&
	git worktree add --detach orig &&
	echo /invalid >orig/.git &&
	mv orig moved &&
	test_must_fail git worktree repair moved >out 2>err &&
	test_must_be_empty out &&
	test_i18ngrep ".git file broken" err
'

test_expect_success 'repair broken gitdir' '
	test_when_finished "rm -rf orig moved && git worktree prune" &&
	git worktree add --detach orig &&
	sed s,orig/\.git$,moved/.git, .git/worktrees/orig/gitdir >expect &&
	rm .git/worktrees/orig/gitdir &&
	mv orig moved &&
	git worktree repair moved 2>err &&
	test_cmp expect .git/worktrees/orig/gitdir &&
	test_i18ngrep "gitdir unreadable" err
'

test_expect_success 'repair incorrect gitdir' '
	test_when_finished "rm -rf orig moved && git worktree prune" &&
	git worktree add --detach orig &&
	sed s,orig/\.git$,moved/.git, .git/worktrees/orig/gitdir >expect &&
	mv orig moved &&
	git worktree repair moved 2>err &&
	test_cmp expect .git/worktrees/orig/gitdir &&
	test_i18ngrep "gitdir incorrect" err
'

test_expect_success 'repair gitdir (implicit) from linked worktree' '
	test_when_finished "rm -rf orig moved && git worktree prune" &&
	git worktree add --detach orig &&
	sed s,orig/\.git$,moved/.git, .git/worktrees/orig/gitdir >expect &&
	mv orig moved &&
	git -C moved worktree repair 2>err &&
	test_cmp expect .git/worktrees/orig/gitdir &&
	test_i18ngrep "gitdir incorrect" err
'

test_expect_success 'unable to repair gitdir (implicit) from main worktree' '
	test_when_finished "rm -rf orig moved && git worktree prune" &&
	git worktree add --detach orig &&
	cat .git/worktrees/orig/gitdir >expect &&
	mv orig moved &&
	git worktree repair 2>err &&
	test_cmp expect .git/worktrees/orig/gitdir &&
	test_must_be_empty err
'

test_expect_success 'repair multiple gitdir files' '
	test_when_finished "rm -rf orig1 orig2 moved1 moved2 &&
		git worktree prune" &&
	git worktree add --detach orig1 &&
	git worktree add --detach orig2 &&
	sed s,orig1/\.git$,moved1/.git, .git/worktrees/orig1/gitdir >expect1 &&
	sed s,orig2/\.git$,moved2/.git, .git/worktrees/orig2/gitdir >expect2 &&
	mv orig1 moved1 &&
	mv orig2 moved2 &&
	git worktree repair moved1 moved2 2>err &&
	test_cmp expect1 .git/worktrees/orig1/gitdir &&
	test_cmp expect2 .git/worktrees/orig2/gitdir &&
	test_i18ngrep "gitdir incorrect:.*orig1/gitdir$" err &&
	test_i18ngrep "gitdir incorrect:.*orig2/gitdir$" err
'

test_expect_success 'repair moved main and linked worktrees' '
	test_when_finished "rm -rf main side mainmoved sidemoved" &&
	test_create_repo main &&
	test_commit -C main init &&
	git -C main worktree add --detach ../side &&
	sed "s,side/\.git$,sidemoved/.git," \
		main/.git/worktrees/side/gitdir >expect-gitdir &&
	sed "s,main/.git/worktrees/side$,mainmoved/.git/worktrees/side," \
		side/.git >expect-gitfile &&
	mv main mainmoved &&
	mv side sidemoved &&
	git -C mainmoved worktree repair ../sidemoved &&
	test_cmp expect-gitdir mainmoved/.git/worktrees/side/gitdir &&
	test_cmp expect-gitfile sidemoved/.git
'

test_done
