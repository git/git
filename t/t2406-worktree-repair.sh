#!/bin/sh

test_description='test but worktree repair'

. ./test-lib.sh

test_expect_success setup '
	test_cummit init
'

test_expect_success 'skip missing worktree' '
	test_when_finished "but worktree prune" &&
	but worktree add --detach missing &&
	rm -rf missing &&
	but worktree repair >out 2>err &&
	test_must_be_empty out &&
	test_must_be_empty err
'

test_expect_success 'worktree path not directory' '
	test_when_finished "but worktree prune" &&
	but worktree add --detach notdir &&
	rm -rf notdir &&
	>notdir &&
	test_must_fail but worktree repair >out 2>err &&
	test_must_be_empty out &&
	test_i18ngrep "not a directory" err
'

test_expect_success "don't clobber .but repo" '
	test_when_finished "rm -rf repo && but worktree prune" &&
	but worktree add --detach repo &&
	rm -rf repo &&
	test_create_repo repo &&
	test_must_fail but worktree repair >out 2>err &&
	test_must_be_empty out &&
	test_i18ngrep ".but is not a file" err
'

test_corrupt_butfile () {
	butcher=$1 &&
	problem=$2 &&
	repairdir=${3:-.} &&
	test_when_finished 'rm -rf corrupt && but worktree prune' &&
	but worktree add --detach corrupt &&
	but -C corrupt rev-parse --absolute-but-dir >expect &&
	eval "$butcher" &&
	but -C "$repairdir" worktree repair 2>err &&
	test_i18ngrep "$problem" err &&
	but -C corrupt rev-parse --absolute-but-dir >actual &&
	test_cmp expect actual
}

test_expect_success 'repair missing .but file' '
	test_corrupt_butfile "rm -f corrupt/.but" ".but file broken"
'

test_expect_success 'repair bogus .but file' '
	test_corrupt_butfile "echo \"butdir: /nowhere\" >corrupt/.but" \
		".but file broken"
'

test_expect_success 'repair incorrect .but file' '
	test_when_finished "rm -rf other && but worktree prune" &&
	test_create_repo other &&
	other=$(but -C other rev-parse --absolute-but-dir) &&
	test_corrupt_butfile "echo \"butdir: $other\" >corrupt/.but" \
		".but file incorrect"
'

test_expect_success 'repair .but file from main/.but' '
	test_corrupt_butfile "rm -f corrupt/.but" ".but file broken" .but
'

test_expect_success 'repair .but file from linked worktree' '
	test_when_finished "rm -rf other && but worktree prune" &&
	but worktree add --detach other &&
	test_corrupt_butfile "rm -f corrupt/.but" ".but file broken" other
'

test_expect_success 'repair .but file from bare.but' '
	test_when_finished "rm -rf bare.but corrupt && but worktree prune" &&
	but clone --bare . bare.but &&
	but -C bare.but worktree add --detach ../corrupt &&
	but -C corrupt rev-parse --absolute-but-dir >expect &&
	rm -f corrupt/.but &&
	but -C bare.but worktree repair &&
	but -C corrupt rev-parse --absolute-but-dir >actual &&
	test_cmp expect actual
'

test_expect_success 'invalid worktree path' '
	test_must_fail but worktree repair /notvalid >out 2>err &&
	test_must_be_empty out &&
	test_i18ngrep "not a valid path" err
'

test_expect_success 'repo not found; .but not file' '
	test_when_finished "rm -rf not-a-worktree" &&
	test_create_repo not-a-worktree &&
	test_must_fail but worktree repair not-a-worktree >out 2>err &&
	test_must_be_empty out &&
	test_i18ngrep ".but is not a file" err
'

test_expect_success 'repo not found; .but not referencing repo' '
	test_when_finished "rm -rf side not-a-repo && but worktree prune" &&
	but worktree add --detach side &&
	sed s,\.but/worktrees/side$,not-a-repo, side/.but >side/.newbut &&
	mv side/.newbut side/.but &&
	mkdir not-a-repo &&
	test_must_fail but worktree repair side 2>err &&
	test_i18ngrep ".but file does not reference a repository" err
'

test_expect_success 'repo not found; .but file broken' '
	test_when_finished "rm -rf orig moved && but worktree prune" &&
	but worktree add --detach orig &&
	echo /invalid >orig/.but &&
	mv orig moved &&
	test_must_fail but worktree repair moved >out 2>err &&
	test_must_be_empty out &&
	test_i18ngrep ".but file broken" err
'

test_expect_success 'repair broken butdir' '
	test_when_finished "rm -rf orig moved && but worktree prune" &&
	but worktree add --detach orig &&
	sed s,orig/\.but$,moved/.but, .but/worktrees/orig/butdir >expect &&
	rm .but/worktrees/orig/butdir &&
	mv orig moved &&
	but worktree repair moved 2>err &&
	test_cmp expect .but/worktrees/orig/butdir &&
	test_i18ngrep "butdir unreadable" err
'

test_expect_success 'repair incorrect butdir' '
	test_when_finished "rm -rf orig moved && but worktree prune" &&
	but worktree add --detach orig &&
	sed s,orig/\.but$,moved/.but, .but/worktrees/orig/butdir >expect &&
	mv orig moved &&
	but worktree repair moved 2>err &&
	test_cmp expect .but/worktrees/orig/butdir &&
	test_i18ngrep "butdir incorrect" err
'

test_expect_success 'repair butdir (implicit) from linked worktree' '
	test_when_finished "rm -rf orig moved && but worktree prune" &&
	but worktree add --detach orig &&
	sed s,orig/\.but$,moved/.but, .but/worktrees/orig/butdir >expect &&
	mv orig moved &&
	but -C moved worktree repair 2>err &&
	test_cmp expect .but/worktrees/orig/butdir &&
	test_i18ngrep "butdir incorrect" err
'

test_expect_success 'unable to repair butdir (implicit) from main worktree' '
	test_when_finished "rm -rf orig moved && but worktree prune" &&
	but worktree add --detach orig &&
	cat .but/worktrees/orig/butdir >expect &&
	mv orig moved &&
	but worktree repair 2>err &&
	test_cmp expect .but/worktrees/orig/butdir &&
	test_must_be_empty err
'

test_expect_success 'repair multiple butdir files' '
	test_when_finished "rm -rf orig1 orig2 moved1 moved2 &&
		but worktree prune" &&
	but worktree add --detach orig1 &&
	but worktree add --detach orig2 &&
	sed s,orig1/\.but$,moved1/.but, .but/worktrees/orig1/butdir >expect1 &&
	sed s,orig2/\.but$,moved2/.but, .but/worktrees/orig2/butdir >expect2 &&
	mv orig1 moved1 &&
	mv orig2 moved2 &&
	but worktree repair moved1 moved2 2>err &&
	test_cmp expect1 .but/worktrees/orig1/butdir &&
	test_cmp expect2 .but/worktrees/orig2/butdir &&
	test_i18ngrep "butdir incorrect:.*orig1/butdir$" err &&
	test_i18ngrep "butdir incorrect:.*orig2/butdir$" err
'

test_expect_success 'repair moved main and linked worktrees' '
	test_when_finished "rm -rf main side mainmoved sidemoved" &&
	test_create_repo main &&
	test_cummit -C main init &&
	but -C main worktree add --detach ../side &&
	sed "s,side/\.but$,sidemoved/.but," \
		main/.but/worktrees/side/butdir >expect-butdir &&
	sed "s,main/.but/worktrees/side$,mainmoved/.but/worktrees/side," \
		side/.but >expect-butfile &&
	mv main mainmoved &&
	mv side sidemoved &&
	but -C mainmoved worktree repair ../sidemoved &&
	test_cmp expect-butdir mainmoved/.but/worktrees/side/butdir &&
	test_cmp expect-butfile sidemoved/.but
'

test_done
