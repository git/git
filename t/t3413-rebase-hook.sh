#!/bin/sh

test_description='git rebase with its hook(s)'

. ./test-lib.sh

test_expect_success setup '
	echo hello >file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	echo goodbye >file &&
	git add file &&
	test_tick &&
	git commit -m second &&
	git checkout -b side HEAD^ &&
	echo world >git &&
	git add git &&
	test_tick &&
	git commit -m side &&
	git checkout master &&
	git log --pretty=oneline --abbrev-commit --graph --all &&
	git branch test side
'

test_expect_success 'rebase' '
	git checkout test &&
	git reset --hard side &&
	git rebase master &&
	test "z$(cat git)" = zworld
'

test_expect_success 'rebase -i' '
	git checkout test &&
	git reset --hard side &&
	EDITOR=true git rebase -i master &&
	test "z$(cat git)" = zworld
'

test_expect_success 'setup pre-rebase hook' '
	mkdir -p .git/hooks &&
	cat >.git/hooks/pre-rebase <<EOF &&
#!$SHELL_PATH
echo "\$1,\$2" >.git/PRE-REBASE-INPUT
EOF
	chmod +x .git/hooks/pre-rebase
'

test_expect_success 'pre-rebase hook gets correct input (1)' '
	git checkout test &&
	git reset --hard side &&
	git rebase master &&
	test "z$(cat git)" = zworld &&
	test "z$(cat .git/PRE-REBASE-INPUT)" = zmaster,

'

test_expect_success 'pre-rebase hook gets correct input (2)' '
	git checkout test &&
	git reset --hard side &&
	git rebase master test &&
	test "z$(cat git)" = zworld &&
	test "z$(cat .git/PRE-REBASE-INPUT)" = zmaster,test
'

test_expect_success 'pre-rebase hook gets correct input (3)' '
	git checkout test &&
	git reset --hard side &&
	git checkout master &&
	git rebase master test &&
	test "z$(cat git)" = zworld &&
	test "z$(cat .git/PRE-REBASE-INPUT)" = zmaster,test
'

test_expect_success 'pre-rebase hook gets correct input (4)' '
	git checkout test &&
	git reset --hard side &&
	EDITOR=true git rebase -i master &&
	test "z$(cat git)" = zworld &&
	test "z$(cat .git/PRE-REBASE-INPUT)" = zmaster,

'

test_expect_success 'pre-rebase hook gets correct input (5)' '
	git checkout test &&
	git reset --hard side &&
	EDITOR=true git rebase -i master test &&
	test "z$(cat git)" = zworld &&
	test "z$(cat .git/PRE-REBASE-INPUT)" = zmaster,test
'

test_expect_success 'pre-rebase hook gets correct input (6)' '
	git checkout test &&
	git reset --hard side &&
	git checkout master &&
	EDITOR=true git rebase -i master test &&
	test "z$(cat git)" = zworld &&
	test "z$(cat .git/PRE-REBASE-INPUT)" = zmaster,test
'

test_expect_success 'setup pre-rebase hook that fails' '
	mkdir -p .git/hooks &&
	cat >.git/hooks/pre-rebase <<EOF &&
#!$SHELL_PATH
false
EOF
	chmod +x .git/hooks/pre-rebase
'

test_expect_success 'pre-rebase hook stops rebase (1)' '
	git checkout test &&
	git reset --hard side &&
	test_must_fail git rebase master &&
	test "z$(git symbolic-ref HEAD)" = zrefs/heads/test &&
	test 0 = $(git rev-list HEAD...side | wc -l)
'

test_expect_success 'pre-rebase hook stops rebase (2)' '
	git checkout test &&
	git reset --hard side &&
	test_must_fail env EDITOR=: git rebase -i master &&
	test "z$(git symbolic-ref HEAD)" = zrefs/heads/test &&
	test 0 = $(git rev-list HEAD...side | wc -l)
'

test_expect_success 'rebase --no-verify overrides pre-rebase (1)' '
	git checkout test &&
	git reset --hard side &&
	git rebase --no-verify master &&
	test "z$(git symbolic-ref HEAD)" = zrefs/heads/test &&
	test "z$(cat git)" = zworld
'

test_expect_success 'rebase --no-verify overrides pre-rebase (2)' '
	git checkout test &&
	git reset --hard side &&
	EDITOR=true git rebase --no-verify -i master &&
	test "z$(git symbolic-ref HEAD)" = zrefs/heads/test &&
	test "z$(cat git)" = zworld
'

test_done
