#!/bin/sh
#
# Copyright (c) 2010 Erick Mattos
#

test_description='git checkout --orphan

Main Tests for --orphan functionality.'

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
	git diff-tree --quiet master alpha
'

test_expect_success '--orphan creates a new orphan branch from <start_point>' '
	git checkout master &&
	git checkout --orphan beta master^ &&
	test_must_fail git rev-parse --verify HEAD &&
	test "refs/heads/beta" = "$(git symbolic-ref HEAD)" &&
	test_tick &&
	git commit -m "Fourth Commit" &&
	test_must_fail git rev-parse --verify HEAD^ &&
	git diff-tree --quiet master^ beta
'

test_expect_success '--orphan must be rejected with -b' '
	git checkout master &&
	test_must_fail git checkout --orphan new -b newer &&
	test refs/heads/master = "$(git symbolic-ref HEAD)"
'

test_expect_success '--orphan must be rejected with -t' '
	git checkout master &&
	test_must_fail git checkout --orphan new -t master &&
	test refs/heads/master = "$(git symbolic-ref HEAD)"
'

test_expect_success '--orphan ignores branch.autosetupmerge' '
	git checkout master &&
	git config branch.autosetupmerge always &&
	git checkout --orphan gamma &&
	test -z "$(git config branch.gamma.merge)" &&
	test refs/heads/gamma = "$(git symbolic-ref HEAD)" &&
	test_must_fail git rev-parse --verify HEAD^
'

test_expect_success '--orphan makes reflog by default' '
	git checkout master &&
	git config --unset core.logAllRefUpdates &&
	git checkout --orphan delta &&
	test_must_fail git rev-parse --verify delta@{0} &&
	git commit -m Delta &&
	git rev-parse --verify delta@{0}
'

test_expect_success '--orphan does not make reflog when core.logAllRefUpdates = false' '
	git checkout master &&
	git config core.logAllRefUpdates false &&
	git checkout --orphan epsilon &&
	test_must_fail git rev-parse --verify epsilon@{0} &&
	git commit -m Epsilon &&
	test_must_fail git rev-parse --verify epsilon@{0}
'

test_expect_success '--orphan with -l makes reflog when core.logAllRefUpdates = false' '
	git checkout master &&
	git checkout -l --orphan zeta &&
	test_must_fail git rev-parse --verify zeta@{0} &&
	git commit -m Zeta &&
	git rev-parse --verify zeta@{0}
'

test_expect_success 'giving up --orphan not committed when -l and core.logAllRefUpdates = false deletes reflog' '
	git checkout master &&
	git checkout -l --orphan eta &&
	test_must_fail git rev-parse --verify eta@{0} &&
	git checkout master &&
	test_must_fail git rev-parse --verify eta@{0}
'

test_expect_success '--orphan is rejected with an existing name' '
	git checkout master &&
	test_must_fail git checkout --orphan master &&
	test refs/heads/master = "$(git symbolic-ref HEAD)"
'

test_expect_success '--orphan refuses to switch if a merge is needed' '
	git checkout master &&
	git reset --hard &&
	echo local >>"$TEST_FILE" &&
	cat "$TEST_FILE" >"$TEST_FILE.saved" &&
	test_must_fail git checkout --orphan new master^ &&
	test refs/heads/master = "$(git symbolic-ref HEAD)" &&
	test_cmp "$TEST_FILE" "$TEST_FILE.saved" &&
	git diff-index --quiet --cached HEAD &&
	git reset --hard
'

test_done
