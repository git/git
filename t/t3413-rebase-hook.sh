#!/bin/sh

test_description='but rebase with its hook(s)'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	echo hello >file &&
	but add file &&
	test_tick &&
	but cummit -m initial &&
	echo goodbye >file &&
	but add file &&
	test_tick &&
	but cummit -m second &&
	but checkout -b side HEAD^ &&
	echo world >but &&
	but add but &&
	test_tick &&
	but cummit -m side &&
	but checkout main &&
	but log --pretty=oneline --abbrev-cummit --graph --all &&
	but branch test side
'

test_expect_success 'rebase' '
	but checkout test &&
	but reset --hard side &&
	but rebase main &&
	test "z$(cat but)" = zworld
'

test_expect_success 'rebase -i' '
	but checkout test &&
	but reset --hard side &&
	EDITOR=true but rebase -i main &&
	test "z$(cat but)" = zworld
'

test_expect_success 'setup pre-rebase hook' '
	test_hook --setup pre-rebase <<-\EOF
	echo "$1,$2" >.but/PRE-REBASE-INPUT
	EOF
'

test_expect_success 'pre-rebase hook gets correct input (1)' '
	but checkout test &&
	but reset --hard side &&
	but rebase main &&
	test "z$(cat but)" = zworld &&
	test "z$(cat .but/PRE-REBASE-INPUT)" = zmain,

'

test_expect_success 'pre-rebase hook gets correct input (2)' '
	but checkout test &&
	but reset --hard side &&
	but rebase main test &&
	test "z$(cat but)" = zworld &&
	test "z$(cat .but/PRE-REBASE-INPUT)" = zmain,test
'

test_expect_success 'pre-rebase hook gets correct input (3)' '
	but checkout test &&
	but reset --hard side &&
	but checkout main &&
	but rebase main test &&
	test "z$(cat but)" = zworld &&
	test "z$(cat .but/PRE-REBASE-INPUT)" = zmain,test
'

test_expect_success 'pre-rebase hook gets correct input (4)' '
	but checkout test &&
	but reset --hard side &&
	EDITOR=true but rebase -i main &&
	test "z$(cat but)" = zworld &&
	test "z$(cat .but/PRE-REBASE-INPUT)" = zmain,

'

test_expect_success 'pre-rebase hook gets correct input (5)' '
	but checkout test &&
	but reset --hard side &&
	EDITOR=true but rebase -i main test &&
	test "z$(cat but)" = zworld &&
	test "z$(cat .but/PRE-REBASE-INPUT)" = zmain,test
'

test_expect_success 'pre-rebase hook gets correct input (6)' '
	but checkout test &&
	but reset --hard side &&
	but checkout main &&
	EDITOR=true but rebase -i main test &&
	test "z$(cat but)" = zworld &&
	test "z$(cat .but/PRE-REBASE-INPUT)" = zmain,test
'

test_expect_success 'setup pre-rebase hook that fails' '
	test_hook --setup --clobber pre-rebase <<-\EOF
	false
	EOF
'

test_expect_success 'pre-rebase hook stops rebase (1)' '
	but checkout test &&
	but reset --hard side &&
	test_must_fail but rebase main &&
	test "z$(but symbolic-ref HEAD)" = zrefs/heads/test &&
	test 0 = $(but rev-list HEAD...side | wc -l)
'

test_expect_success 'pre-rebase hook stops rebase (2)' '
	but checkout test &&
	but reset --hard side &&
	test_must_fail env EDITOR=: but rebase -i main &&
	test "z$(but symbolic-ref HEAD)" = zrefs/heads/test &&
	test 0 = $(but rev-list HEAD...side | wc -l)
'

test_expect_success 'rebase --no-verify overrides pre-rebase (1)' '
	but checkout test &&
	but reset --hard side &&
	but rebase --no-verify main &&
	test "z$(but symbolic-ref HEAD)" = zrefs/heads/test &&
	test "z$(cat but)" = zworld
'

test_expect_success 'rebase --no-verify overrides pre-rebase (2)' '
	but checkout test &&
	but reset --hard side &&
	EDITOR=true but rebase --no-verify -i main &&
	test "z$(but symbolic-ref HEAD)" = zrefs/heads/test &&
	test "z$(cat but)" = zworld
'

test_done
