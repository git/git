#!/bin/sh

test_description='prune $GIT_DIR/worktrees'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success initialize '
	but cummit --allow-empty -m init
'

test_expect_success 'worktree prune on normal repo' '
	but worktree prune &&
	test_must_fail but worktree prune abc
'

test_expect_success 'prune files inside $GIT_DIR/worktrees' '
	mkdir .but/worktrees &&
	: >.but/worktrees/abc &&
	but worktree prune --verbose 2>actual &&
	cat >expect <<EOF &&
Removing worktrees/abc: not a valid directory
EOF
	test_cmp expect actual &&
	! test -f .but/worktrees/abc &&
	! test -d .but/worktrees
'

test_expect_success 'prune directories without butdir' '
	mkdir -p .but/worktrees/def/abc &&
	: >.but/worktrees/def/def &&
	cat >expect <<EOF &&
Removing worktrees/def: butdir file does not exist
EOF
	but worktree prune --verbose 2>actual &&
	test_cmp expect actual &&
	! test -d .but/worktrees/def &&
	! test -d .but/worktrees
'

test_expect_success SANITY 'prune directories with unreadable butdir' '
	mkdir -p .but/worktrees/def/abc &&
	: >.but/worktrees/def/def &&
	: >.but/worktrees/def/butdir &&
	chmod u-r .but/worktrees/def/butdir &&
	but worktree prune --verbose 2>actual &&
	test_i18ngrep "Removing worktrees/def: unable to read butdir file" actual &&
	! test -d .but/worktrees/def &&
	! test -d .but/worktrees
'

test_expect_success 'prune directories with invalid butdir' '
	mkdir -p .but/worktrees/def/abc &&
	: >.but/worktrees/def/def &&
	: >.but/worktrees/def/butdir &&
	but worktree prune --verbose 2>actual &&
	test_i18ngrep "Removing worktrees/def: invalid butdir file" actual &&
	! test -d .but/worktrees/def &&
	! test -d .but/worktrees
'

test_expect_success 'prune directories with butdir pointing to nowhere' '
	mkdir -p .but/worktrees/def/abc &&
	: >.but/worktrees/def/def &&
	echo "$(pwd)"/nowhere >.but/worktrees/def/butdir &&
	but worktree prune --verbose 2>actual &&
	test_i18ngrep "Removing worktrees/def: butdir file points to non-existent location" actual &&
	! test -d .but/worktrees/def &&
	! test -d .but/worktrees
'

test_expect_success 'not prune locked checkout' '
	test_when_finished rm -r .but/worktrees &&
	mkdir -p .but/worktrees/ghi &&
	: >.but/worktrees/ghi/locked &&
	but worktree prune &&
	test -d .but/worktrees/ghi
'

test_expect_success 'not prune recent checkouts' '
	test_when_finished rm -r .but/worktrees &&
	but worktree add jlm HEAD &&
	test -d .but/worktrees/jlm &&
	rm -rf jlm &&
	but worktree prune --verbose --expire=2.days.ago &&
	test -d .but/worktrees/jlm
'

test_expect_success 'not prune proper checkouts' '
	test_when_finished rm -r .but/worktrees &&
	but worktree add --detach "$PWD/nop" main &&
	but worktree prune &&
	test -d .but/worktrees/nop
'

test_expect_success 'prune duplicate (linked/linked)' '
	test_when_finished rm -fr .but/worktrees w1 w2 &&
	but worktree add --detach w1 &&
	but worktree add --detach w2 &&
	sed "s/w2/w1/" .but/worktrees/w2/butdir >.but/worktrees/w2/butdir.new &&
	mv .but/worktrees/w2/butdir.new .but/worktrees/w2/butdir &&
	but worktree prune --verbose 2>actual &&
	test_i18ngrep "duplicate entry" actual &&
	test -d .but/worktrees/w1 &&
	! test -d .but/worktrees/w2
'

test_expect_success 'prune duplicate (main/linked)' '
	test_when_finished rm -fr repo wt &&
	test_create_repo repo &&
	test_cummit -C repo x &&
	but -C repo worktree add --detach ../wt &&
	rm -fr wt &&
	mv repo wt &&
	but -C wt worktree prune --verbose 2>actual &&
	test_i18ngrep "duplicate entry" actual &&
	! test -d .but/worktrees/wt
'

test_done
