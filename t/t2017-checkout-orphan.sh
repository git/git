#!/bin/sh
#
# Copyright (c) 2010 Erick Mattos
#

test_description='git checkout --orphan

Main Tests for --orphan functionality.'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

TEST_FILE=foo

test_expect_success 'Setup' '
	echo "Initial" >"$TEST_FILE" &&
	git add "$TEST_FILE" &&
	git commit -m "First Commit" &&
	test_tick &&
	echo "State 1" >>"$TEST_FILE" &&
	git add "$TEST_FILE" &&
	test_tick &&
	git commit -m "Second Commit"
'

test_expect_success '--orphan creates a new orphan branch from HEAD' '
	git checkout --orphan alpha &&
	test_must_fail git rev-parse --verify HEAD &&
	test "refs/heads/alpha" = "$(git symbolic-ref HEAD)" &&
	test_tick &&
	git commit -m "Third Commit" &&
	test_must_fail git rev-parse --verify HEAD^ &&
	git diff-tree --quiet main alpha
'

test_expect_success '--orphan creates a new orphan branch from <start_point>' '
	git checkout main &&
	git checkout --orphan beta main^ &&
	test_must_fail git rev-parse --verify HEAD &&
	test "refs/heads/beta" = "$(git symbolic-ref HEAD)" &&
	test_tick &&
	git commit -m "Fourth Commit" &&
	test_must_fail git rev-parse --verify HEAD^ &&
	git diff-tree --quiet main^ beta
'

test_expect_success '--orphan must be rejected with -b' '
	git checkout main &&
	test_must_fail git checkout --orphan new -b newer &&
	test refs/heads/main = "$(git symbolic-ref HEAD)"
'

test_expect_success '--orphan must be rejected with -t' '
	git checkout main &&
	test_must_fail git checkout --orphan new -t main &&
	test refs/heads/main = "$(git symbolic-ref HEAD)"
'

test_expect_success '--orphan ignores branch.autosetupmerge' '
	git checkout main &&
	git config branch.autosetupmerge always &&
	git checkout --orphan gamma &&
	test_cmp_config "" --default "" branch.gamma.merge &&
	test refs/heads/gamma = "$(git symbolic-ref HEAD)" &&
	test_must_fail git rev-parse --verify HEAD^ &&
	git checkout main &&
	git config branch.autosetupmerge inherit &&
	git checkout --orphan eta &&
	test_cmp_config "" --default "" branch.eta.merge &&
	test_cmp_config "" --default "" branch.eta.remote &&
	echo refs/heads/eta >expected &&
	git symbolic-ref HEAD >actual &&
	test_cmp expected actual &&
	test_must_fail git rev-parse --verify HEAD^
'

test_expect_success '--orphan makes reflog by default' '
	git checkout main &&
	git config --unset core.logAllRefUpdates &&
	git checkout --orphan delta &&
	test_must_fail git rev-parse --verify delta@{0} &&
	git commit -m Delta &&
	git rev-parse --verify delta@{0}
'

test_expect_success '--orphan does not make reflog when core.logAllRefUpdates = false' '
	git checkout main &&
	git config core.logAllRefUpdates false &&
	git checkout --orphan epsilon &&
	test_must_fail git rev-parse --verify epsilon@{0} &&
	git commit -m Epsilon &&
	test_must_fail git rev-parse --verify epsilon@{0}
'

test_expect_success '--orphan with -l makes reflog when core.logAllRefUpdates = false' '
	git checkout main &&
	git checkout -l --orphan zeta &&
	test_must_fail git rev-parse --verify zeta@{0} &&
	git commit -m Zeta &&
	git rev-parse --verify zeta@{0}
'

test_expect_success 'giving up --orphan not committed when -l and core.logAllRefUpdates = false deletes reflog' '
	git checkout main &&
	git checkout -l --orphan eta &&
	test_must_fail git rev-parse --verify eta@{0} &&
	git checkout main &&
	test_must_fail git rev-parse --verify eta@{0}
'

test_expect_success '--orphan is rejected with an existing name' '
	git checkout main &&
	test_must_fail git checkout --orphan main &&
	test refs/heads/main = "$(git symbolic-ref HEAD)"
'

test_expect_success '--orphan refuses to switch if a merge is needed' '
	git checkout main &&
	git reset --hard &&
	echo local >>"$TEST_FILE" &&
	cat "$TEST_FILE" >"$TEST_FILE.saved" &&
	test_must_fail git checkout --orphan new main^ &&
	test refs/heads/main = "$(git symbolic-ref HEAD)" &&
	test_cmp "$TEST_FILE" "$TEST_FILE.saved" &&
	git diff-index --quiet --cached HEAD &&
	git reset --hard
'

test_expect_success 'cannot --detach on an unborn branch' '
	git checkout main &&
	git checkout --orphan new &&
	test_must_fail git checkout --detach
'

test_done
