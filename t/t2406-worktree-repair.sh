#!/bin/sh

test_description='test git worktree repair'

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
	git -C "$repairdir" worktree repair >out 2>err &&
	test_i18ngrep "$problem" out &&
	test_must_be_empty err &&
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

test_done
