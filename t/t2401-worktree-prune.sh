#!/bin/sh

test_description='prune $GIT_DIR/worktrees'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success initialize '
	git commit --allow-empty -m init
'

test_expect_success 'worktree prune on normal repo' '
	git worktree prune &&
	test_must_fail git worktree prune abc
'

test_expect_success 'prune files inside $GIT_DIR/worktrees' '
	mkdir .git/worktrees &&
	: >.git/worktrees/abc &&
	git worktree prune --verbose 2>actual &&
	cat >expect <<EOF &&
Removing worktrees/abc: not a valid directory
EOF
	test_cmp expect actual &&
	! test -f .git/worktrees/abc &&
	! test -d .git/worktrees
'

test_expect_success 'prune directories without gitdir' '
	mkdir -p .git/worktrees/def/abc &&
	: >.git/worktrees/def/def &&
	cat >expect <<EOF &&
Removing worktrees/def: gitdir file does not exist
EOF
	git worktree prune --verbose 2>actual &&
	test_cmp expect actual &&
	! test -d .git/worktrees/def &&
	! test -d .git/worktrees
'

test_expect_success SANITY 'prune directories with unreadable gitdir' '
	mkdir -p .git/worktrees/def/abc &&
	: >.git/worktrees/def/def &&
	: >.git/worktrees/def/gitdir &&
	chmod u-r .git/worktrees/def/gitdir &&
	git worktree prune --verbose 2>actual &&
	test_i18ngrep "Removing worktrees/def: unable to read gitdir file" actual &&
	! test -d .git/worktrees/def &&
	! test -d .git/worktrees
'

test_expect_success 'prune directories with invalid gitdir' '
	mkdir -p .git/worktrees/def/abc &&
	: >.git/worktrees/def/def &&
	: >.git/worktrees/def/gitdir &&
	git worktree prune --verbose 2>actual &&
	test_i18ngrep "Removing worktrees/def: invalid gitdir file" actual &&
	! test -d .git/worktrees/def &&
	! test -d .git/worktrees
'

test_expect_success 'prune directories with gitdir pointing to nowhere' '
	mkdir -p .git/worktrees/def/abc &&
	: >.git/worktrees/def/def &&
	echo "$(pwd)"/nowhere >.git/worktrees/def/gitdir &&
	git worktree prune --verbose 2>actual &&
	test_i18ngrep "Removing worktrees/def: gitdir file points to non-existent location" actual &&
	! test -d .git/worktrees/def &&
	! test -d .git/worktrees
'

test_expect_success 'not prune locked checkout' '
	test_when_finished rm -r .git/worktrees &&
	mkdir -p .git/worktrees/ghi &&
	: >.git/worktrees/ghi/locked &&
	git worktree prune &&
	test -d .git/worktrees/ghi
'

test_expect_success 'not prune recent checkouts' '
	test_when_finished rm -r .git/worktrees &&
	git worktree add jlm HEAD &&
	test -d .git/worktrees/jlm &&
	rm -rf jlm &&
	git worktree prune --verbose --expire=2.days.ago &&
	test -d .git/worktrees/jlm
'

test_expect_success 'not prune proper checkouts' '
	test_when_finished rm -r .git/worktrees &&
	git worktree add --detach "$PWD/nop" main &&
	git worktree prune &&
	test -d .git/worktrees/nop
'

test_expect_success 'prune duplicate (linked/linked)' '
	test_when_finished rm -fr .git/worktrees w1 w2 &&
	git worktree add --detach w1 &&
	git worktree add --detach w2 &&
	sed "s/w2/w1/" .git/worktrees/w2/gitdir >.git/worktrees/w2/gitdir.new &&
	mv .git/worktrees/w2/gitdir.new .git/worktrees/w2/gitdir &&
	git worktree prune --verbose 2>actual &&
	test_i18ngrep "duplicate entry" actual &&
	test -d .git/worktrees/w1 &&
	! test -d .git/worktrees/w2
'

test_expect_success 'prune duplicate (main/linked)' '
	test_when_finished rm -fr repo wt &&
	test_create_repo repo &&
	test_commit -C repo x &&
	git -C repo worktree add --detach ../wt &&
	rm -fr wt &&
	mv repo wt &&
	git -C wt worktree prune --verbose 2>actual &&
	test_i18ngrep "duplicate entry" actual &&
	! test -d .git/worktrees/wt
'

test_done
