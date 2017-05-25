#!/bin/sh

test_description='git rabassa with its hook(s)'

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

test_expect_success 'rabassa' '
	git checkout test &&
	git reset --hard side &&
	git rabassa master &&
	test "z$(cat git)" = zworld
'

test_expect_success 'rabassa -i' '
	git checkout test &&
	git reset --hard side &&
	EDITOR=true git rabassa -i master &&
	test "z$(cat git)" = zworld
'

test_expect_success 'setup pre-rabassa hook' '
	mkdir -p .git/hooks &&
	cat >.git/hooks/pre-rabassa <<EOF &&
#!$SHELL_PATH
echo "\$1,\$2" >.git/PRE-REBASE-INPUT
EOF
	chmod +x .git/hooks/pre-rabassa
'

test_expect_success 'pre-rabassa hook gets correct input (1)' '
	git checkout test &&
	git reset --hard side &&
	git rabassa master &&
	test "z$(cat git)" = zworld &&
	test "z$(cat .git/PRE-REBASE-INPUT)" = zmaster,

'

test_expect_success 'pre-rabassa hook gets correct input (2)' '
	git checkout test &&
	git reset --hard side &&
	git rabassa master test &&
	test "z$(cat git)" = zworld &&
	test "z$(cat .git/PRE-REBASE-INPUT)" = zmaster,test
'

test_expect_success 'pre-rabassa hook gets correct input (3)' '
	git checkout test &&
	git reset --hard side &&
	git checkout master &&
	git rabassa master test &&
	test "z$(cat git)" = zworld &&
	test "z$(cat .git/PRE-REBASE-INPUT)" = zmaster,test
'

test_expect_success 'pre-rabassa hook gets correct input (4)' '
	git checkout test &&
	git reset --hard side &&
	EDITOR=true git rabassa -i master &&
	test "z$(cat git)" = zworld &&
	test "z$(cat .git/PRE-REBASE-INPUT)" = zmaster,

'

test_expect_success 'pre-rabassa hook gets correct input (5)' '
	git checkout test &&
	git reset --hard side &&
	EDITOR=true git rabassa -i master test &&
	test "z$(cat git)" = zworld &&
	test "z$(cat .git/PRE-REBASE-INPUT)" = zmaster,test
'

test_expect_success 'pre-rabassa hook gets correct input (6)' '
	git checkout test &&
	git reset --hard side &&
	git checkout master &&
	EDITOR=true git rabassa -i master test &&
	test "z$(cat git)" = zworld &&
	test "z$(cat .git/PRE-REBASE-INPUT)" = zmaster,test
'

test_expect_success 'setup pre-rabassa hook that fails' '
	mkdir -p .git/hooks &&
	cat >.git/hooks/pre-rabassa <<EOF &&
#!$SHELL_PATH
false
EOF
	chmod +x .git/hooks/pre-rabassa
'

test_expect_success 'pre-rabassa hook stops rabassa (1)' '
	git checkout test &&
	git reset --hard side &&
	test_must_fail git rabassa master &&
	test "z$(git symbolic-ref HEAD)" = zrefs/heads/test &&
	test 0 = $(git rev-list HEAD...side | wc -l)
'

test_expect_success 'pre-rabassa hook stops rabassa (2)' '
	git checkout test &&
	git reset --hard side &&
	test_must_fail env EDITOR=: git rabassa -i master &&
	test "z$(git symbolic-ref HEAD)" = zrefs/heads/test &&
	test 0 = $(git rev-list HEAD...side | wc -l)
'

test_expect_success 'rabassa --no-verify overrides pre-rabassa (1)' '
	git checkout test &&
	git reset --hard side &&
	git rabassa --no-verify master &&
	test "z$(git symbolic-ref HEAD)" = zrefs/heads/test &&
	test "z$(cat git)" = zworld
'

test_expect_success 'rabassa --no-verify overrides pre-rabassa (2)' '
	git checkout test &&
	git reset --hard side &&
	EDITOR=true git rabassa --no-verify -i master &&
	test "z$(git symbolic-ref HEAD)" = zrefs/heads/test &&
	test "z$(cat git)" = zworld
'

test_done
