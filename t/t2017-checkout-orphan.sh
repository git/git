#!/bin/sh
#
# Copyright (c) 2010 Erick Mattos
#

test_description='but checkout --orphan

Main Tests for --orphan functionality.'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

TEST_FILE=foo

test_expect_success 'Setup' '
	echo "Initial" >"$TEST_FILE" &&
	but add "$TEST_FILE" &&
	but cummit -m "First cummit" &&
	test_tick &&
	echo "State 1" >>"$TEST_FILE" &&
	but add "$TEST_FILE" &&
	test_tick &&
	but cummit -m "Second cummit"
'

test_expect_success '--orphan creates a new orphan branch from HEAD' '
	but checkout --orphan alpha &&
	test_must_fail but rev-parse --verify HEAD &&
	test "refs/heads/alpha" = "$(but symbolic-ref HEAD)" &&
	test_tick &&
	but cummit -m "Third cummit" &&
	test_must_fail but rev-parse --verify HEAD^ &&
	but diff-tree --quiet main alpha
'

test_expect_success '--orphan creates a new orphan branch from <start_point>' '
	but checkout main &&
	but checkout --orphan beta main^ &&
	test_must_fail but rev-parse --verify HEAD &&
	test "refs/heads/beta" = "$(but symbolic-ref HEAD)" &&
	test_tick &&
	but cummit -m "Fourth cummit" &&
	test_must_fail but rev-parse --verify HEAD^ &&
	but diff-tree --quiet main^ beta
'

test_expect_success '--orphan must be rejected with -b' '
	but checkout main &&
	test_must_fail but checkout --orphan new -b newer &&
	test refs/heads/main = "$(but symbolic-ref HEAD)"
'

test_expect_success '--orphan must be rejected with -t' '
	but checkout main &&
	test_must_fail but checkout --orphan new -t main &&
	test refs/heads/main = "$(but symbolic-ref HEAD)"
'

test_expect_success '--orphan ignores branch.autosetupmerge' '
	but checkout main &&
	but config branch.autosetupmerge always &&
	but checkout --orphan gamma &&
	test_cmp_config "" --default "" branch.gamma.merge &&
	test refs/heads/gamma = "$(but symbolic-ref HEAD)" &&
	test_must_fail but rev-parse --verify HEAD^ &&
	but checkout main &&
	but config branch.autosetupmerge inherit &&
	but checkout --orphan eta &&
	test_cmp_config "" --default "" branch.eta.merge &&
	test_cmp_config "" --default "" branch.eta.remote &&
	echo refs/heads/eta >expected &&
	but symbolic-ref HEAD >actual &&
	test_cmp expected actual &&
	test_must_fail but rev-parse --verify HEAD^
'

test_expect_success '--orphan makes reflog by default' '
	but checkout main &&
	but config --unset core.logAllRefUpdates &&
	but checkout --orphan delta &&
	test_must_fail but rev-parse --verify delta@{0} &&
	but cummit -m Delta &&
	but rev-parse --verify delta@{0}
'

test_expect_success REFFILES '--orphan does not make reflog when core.logAllRefUpdates = false' '
	but checkout main &&
	but config core.logAllRefUpdates false &&
	but checkout --orphan epsilon &&
	test_must_fail but rev-parse --verify epsilon@{0} &&
	but cummit -m Epsilon &&
	test_must_fail but rev-parse --verify epsilon@{0}
'

test_expect_success '--orphan with -l makes reflog when core.logAllRefUpdates = false' '
	but checkout main &&
	but checkout -l --orphan zeta &&
	test_must_fail but rev-parse --verify zeta@{0} &&
	but cummit -m Zeta &&
	but rev-parse --verify zeta@{0}
'

test_expect_success 'giving up --orphan not cummitted when -l and core.logAllRefUpdates = false deletes reflog' '
	but checkout main &&
	but checkout -l --orphan eta &&
	test_must_fail but rev-parse --verify eta@{0} &&
	but checkout main &&
	test_must_fail but rev-parse --verify eta@{0}
'

test_expect_success '--orphan is rejected with an existing name' '
	but checkout main &&
	test_must_fail but checkout --orphan main &&
	test refs/heads/main = "$(but symbolic-ref HEAD)"
'

test_expect_success '--orphan refuses to switch if a merge is needed' '
	but checkout main &&
	but reset --hard &&
	echo local >>"$TEST_FILE" &&
	cat "$TEST_FILE" >"$TEST_FILE.saved" &&
	test_must_fail but checkout --orphan new main^ &&
	test refs/heads/main = "$(but symbolic-ref HEAD)" &&
	test_cmp "$TEST_FILE" "$TEST_FILE.saved" &&
	but diff-index --quiet --cached HEAD &&
	but reset --hard
'

test_expect_success 'cannot --detach on an unborn branch' '
	but checkout main &&
	but checkout --orphan new &&
	test_must_fail but checkout --detach
'

test_done
