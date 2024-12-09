#!/bin/sh

test_description='check various push.default settings'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup bare remotes' '
	git init --bare repo1 &&
	git remote add parent1 repo1 &&
	git init --bare repo2 &&
	git remote add parent2 repo2 &&
	test_commit one &&
	git push parent1 HEAD &&
	git push parent2 HEAD
'

# $1 = local revision
# $2 = remote revision (tested to be equal to the local one)
# $3 = [optional] repo to check for actual output (repo1 by default)
check_pushed_commit () {
	git log -1 --format='%h %s' "$1" >expect &&
	git --git-dir="${3:-repo1}" log -1 --format='%h %s' "$2" >actual &&
	test_cmp expect actual
}

# $1 = push.default value
# $2 = expected target branch for the push
# $3 = [optional] repo to check for actual output (repo1 by default)
test_push_success () {
	git ${1:+-c} ${1:+push.default="$1"} push &&
	check_pushed_commit HEAD "$2" "$3"
}

# $1 = push.default value
# check that push fails and does not modify any remote branch
test_push_failure () {
	git --git-dir=repo1 log --no-walk --format='%h %s' --all >expect &&
	test_must_fail git ${1:+-c} ${1:+push.default="$1"} push &&
	git --git-dir=repo1 log --no-walk --format='%h %s' --all >actual &&
	test_cmp expect actual
}

# $1 = success or failure
# $2 = push.default value
# $3 = branch to check for actual output (main or foo)
# $4 = [optional] switch to triangular workflow
test_pushdefault_workflow () {
	workflow=central
	pushdefault=parent1
	if test -n "${4-}"; then
		workflow=triangular
		pushdefault=parent2
	fi
	test_expect_success "push.default = $2 $1 in $workflow workflows" "
		test_config branch.main.remote parent1 &&
		test_config branch.main.merge refs/heads/foo &&
		test_config remote.pushdefault $pushdefault &&
		test_commit commit-for-$2${4+-triangular} &&
		test_push_$1 $2 $3 ${4+repo2}
	"
}

test_expect_success '"upstream" pushes to configured upstream' '
	git checkout main &&
	test_config branch.main.remote parent1 &&
	test_config branch.main.merge refs/heads/foo &&
	test_commit two &&
	test_push_success upstream foo
'

test_expect_success '"upstream" does not push on unconfigured remote' '
	git checkout main &&
	test_unconfig branch.main.remote &&
	test_commit three &&
	test_push_failure upstream
'

test_expect_success '"upstream" does not push on unconfigured branch' '
	git checkout main &&
	test_config branch.main.remote parent1 &&
	test_unconfig branch.main.merge &&
	test_commit four &&
	test_push_failure upstream
'

test_expect_success '"upstream" does not push when remotes do not match' '
	git checkout main &&
	test_config branch.main.remote parent1 &&
	test_config branch.main.merge refs/heads/foo &&
	test_config push.default upstream &&
	test_commit five &&
	test_must_fail git push parent2
'

test_expect_success '"current" does not push when multiple remotes and none origin' '
	git checkout main &&
	test_config push.default current &&
	test_commit current-multi &&
	test_must_fail git push
'

test_expect_success '"current" pushes when remote explicitly specified' '
	git checkout main &&
	test_config push.default current &&
	test_commit current-specified &&
	git push parent1
'

test_expect_success '"current" pushes to origin when no remote specified among multiple' '
	git checkout main &&
	test_config remote.origin.url repo1 &&
	test_config remote.origin.fetch "+refs/heads/*:refs/remotes/origin/*" &&
	test_commit current-origin &&
	test_push_success current main
'

test_expect_success '"current" pushes to single remote even when not specified' '
	git checkout main &&
	test_when_finished git remote add parent1 repo1 &&
	git remote remove parent1 &&
	test_commit current-implied &&
	test_push_success current main repo2
'

test_expect_success 'push from/to new branch with non-defaulted remote fails with upstream, matching, current and simple ' '
	git checkout -b new-branch &&
	test_push_failure simple &&
	test_push_failure matching &&
	test_push_failure upstream &&
	test_push_failure current
'

test_expect_success 'push from/to new branch fails with upstream and simple ' '
	git checkout -b new-branch-1 &&
	test_config branch.new-branch-1.remote parent1 &&
	test_push_failure simple &&
	test_push_failure upstream
'

# The behavior here is surprising but not entirely wrong:
#  - the current branch is used to determine the target remote
#  - the "matching" push default pushes matching branches, *ignoring* the
#       current new branch as it does not have upstream tracking
#  - the default push succeeds
#
# A previous test expected this to fail, but for the wrong reasons:
# it expected to fail because the branch is new and cannot be pushed, but
# in fact it was failing because of an ambiguous remote
#
test_expect_failure 'push from/to new branch fails with matching ' '
	git checkout -b new-branch-2 &&
	test_config branch.new-branch-2.remote parent1 &&
	test_push_failure matching
'

test_expect_success 'push from/to branch with tracking fails with nothing ' '
	git checkout -b tracked-branch &&
	test_config branch.tracked-branch.remote parent1 &&
	test_config branch.tracked-branch.merge refs/heads/tracked-branch &&
	test_push_failure nothing
'

test_expect_success 'push from/to new branch succeeds with upstream if push.autoSetupRemote' '
	git checkout -b new-branch-a &&
	test_config push.autoSetupRemote true &&
	test_config branch.new-branch-a.remote parent1 &&
	test_push_success upstream new-branch-a
'

test_expect_success 'push from/to new branch succeeds with simple if push.autoSetupRemote' '
	git checkout -b new-branch-c &&
	test_config push.autoSetupRemote true &&
	test_config branch.new-branch-c.remote parent1 &&
	test_push_success simple new-branch-c
'

test_expect_success '"matching" fails if none match' '
	git init --bare empty &&
	test_must_fail git push empty : 2>actual &&
	test_grep "Perhaps you should specify a branch" actual
'

test_expect_success 'push ambiguously named branch with upstream, matching and simple' '
	git checkout -b ambiguous &&
	test_config branch.ambiguous.remote parent1 &&
	test_config branch.ambiguous.merge refs/heads/ambiguous &&
	git tag ambiguous &&
	test_push_success simple ambiguous &&
	test_push_success matching ambiguous &&
	test_push_success upstream ambiguous
'

test_expect_success 'push from/to new branch with current creates remote branch' '
	test_config branch.new-branch.remote repo1 &&
	git checkout new-branch &&
	test_push_success current new-branch
'

test_expect_success 'push to existing branch, with no upstream configured' '
	test_config branch.main.remote repo1 &&
	git checkout main &&
	test_push_failure simple &&
	test_push_failure upstream
'

test_expect_success 'push to existing branch, upstream configured with same name' '
	test_config branch.main.remote repo1 &&
	test_config branch.main.merge refs/heads/main &&
	git checkout main &&
	test_commit six &&
	test_push_success upstream main &&
	test_commit seven &&
	test_push_success simple main
'

test_expect_success 'push to existing branch, upstream configured with different name' '
	test_config branch.main.remote repo1 &&
	test_config branch.main.merge refs/heads/other-name &&
	git checkout main &&
	test_commit eight &&
	test_push_success upstream other-name &&
	test_commit nine &&
	test_push_failure simple &&
	git --git-dir=repo1 log -1 --format="%h %s" "other-name" >expect-other-name &&
	test_push_success current main &&
	git --git-dir=repo1 log -1 --format="%h %s" "other-name" >actual-other-name &&
	test_cmp expect-other-name actual-other-name
'

# We are on 'main', which integrates with 'foo' from parent1
# remote (set in test_pushdefault_workflow helper).  Push to
# parent1 in centralized, and push to parent2 in triangular workflow.
# The parent1 repository has 'main' and 'foo' branches, while
# the parent2 repository has only 'main' branch.
#
# test_pushdefault_workflow() arguments:
# $1 = success or failure
# $2 = push.default value
# $3 = branch to check for actual output (main or foo)
# $4 = [optional] switch to triangular workflow

# update parent1's main (which is not our upstream)
test_pushdefault_workflow success current main

# update parent1's foo (which is our upstream)
test_pushdefault_workflow success upstream foo

# upstream is foo which is not the name of the current branch
test_pushdefault_workflow failure simple main

# main and foo are updated
test_pushdefault_workflow success matching main

# main is updated
test_pushdefault_workflow success current main triangular

# upstream mode cannot be used in triangular
test_pushdefault_workflow failure upstream foo triangular

# in triangular, 'simple' works as 'current' and update the branch
# with the same name.
test_pushdefault_workflow success simple main triangular

# main is updated (parent2 does not have foo)
test_pushdefault_workflow success matching main triangular

# default tests, when no push-default is specified. This
# should behave the same as "simple" in non-triangular
# settings, and as "current" otherwise.

test_expect_success 'default behavior allows "simple" push' '
	test_config branch.main.remote parent1 &&
	test_config branch.main.merge refs/heads/main &&
	test_config remote.pushdefault parent1 &&
	test_commit default-main-main &&
	test_push_success "" main
'

test_expect_success 'default behavior rejects non-simple push' '
	test_config branch.main.remote parent1 &&
	test_config branch.main.merge refs/heads/foo &&
	test_config remote.pushdefault parent1 &&
	test_commit default-main-foo &&
	test_push_failure ""
'

test_expect_success 'default triangular behavior acts like "current"' '
	test_config branch.main.remote parent1 &&
	test_config branch.main.merge refs/heads/foo &&
	test_config remote.pushdefault parent2 &&
	test_commit default-triangular &&
	test_push_success "" main repo2
'

test_done
