#!/bin/sh

test_description='git branch submodule tests'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

test_expect_success 'setup superproject and submodule' '
	git init super &&
	test_commit foo &&
	git init sub-sub-upstream &&
	test_commit -C sub-sub-upstream foo &&
	git init sub-upstream &&
	# Submodule in a submodule
	git -C sub-upstream submodule add "$TRASH_DIRECTORY/sub-sub-upstream" sub-sub &&
	git -C sub-upstream commit -m "add submodule" &&
	# Regular submodule
	git -C super submodule add "$TRASH_DIRECTORY/sub-upstream" sub &&
	# Submodule in a subdirectory
	git -C super submodule add "$TRASH_DIRECTORY/sub-sub-upstream" second/sub &&
	git -C super commit -m "add submodule" &&
	git -C super config submodule.propagateBranches true &&
	git -C super/sub submodule update --init
'

CLEANUP_SCRIPT_PATH="$TRASH_DIRECTORY/cleanup_branches.sh"

cat >"$CLEANUP_SCRIPT_PATH" <<'EOF'
	#!/bin/sh

	super_dir="$1"
	shift
	(
		cd "$super_dir" &&
		git checkout main &&
		for branch_name in "$@"; do
			git branch -D "$branch_name"
			git submodule foreach "$TRASH_DIRECTORY/cleanup_branches.sh . $branch_name || true"
		done
	)
EOF
chmod +x "$CLEANUP_SCRIPT_PATH"

cleanup_branches () {
	TRASH_DIRECTORY="\"$TRASH_DIRECTORY\"" "$CLEANUP_SCRIPT_PATH" "$@"
} >/dev/null 2>/dev/null

# Test the argument parsing
test_expect_success '--recurse-submodules should create branches' '
	test_when_finished "cleanup_branches super branch-a" &&
	(
		cd super &&
		git branch --recurse-submodules branch-a &&
		git rev-parse branch-a &&
		git -C sub rev-parse branch-a &&
		git -C sub/sub-sub rev-parse branch-a &&
		git -C second/sub rev-parse branch-a
	)
'

test_expect_success '--recurse-submodules should die if submodule.propagateBranches is false' '
	test_when_finished "cleanup_branches super branch-a" &&
	(
		cd super &&
		echo "fatal: branch with --recurse-submodules can only be used if submodule.propagateBranches is enabled" >expected &&
		test_must_fail git -c submodule.propagateBranches=false branch --recurse-submodules branch-a 2>actual &&
		test_cmp expected actual
	)
'

test_expect_success '--recurse-submodules should fail when not creating branches' '
	test_when_finished "cleanup_branches super branch-a" &&
	(
		cd super &&
		git branch --recurse-submodules branch-a &&
		test_must_fail git branch --recurse-submodules -D branch-a &&
		# Assert that the branches were not deleted
		git rev-parse --abbrev-ref branch-a &&
		git -C sub rev-parse --abbrev-ref branch-a
	)
'

test_expect_success 'should respect submodule.recurse when creating branches' '
	test_when_finished "cleanup_branches super branch-a" &&
	(
		cd super &&
		git -c submodule.recurse=true branch branch-a &&
		git rev-parse --abbrev-ref branch-a &&
		git -C sub rev-parse --abbrev-ref branch-a
	)
'

test_expect_success 'should ignore submodule.recurse when not creating branches' '
	test_when_finished "cleanup_branches super branch-a" &&
	(
		cd super &&
		git branch --recurse-submodules branch-a &&
		git -c submodule.recurse=true branch -D branch-a &&
		test_must_fail git rev-parse --abbrev-ref branch-a &&
		git -C sub rev-parse --abbrev-ref branch-a
	)
'

# Test branch creation behavior
test_expect_success 'should create branches based off commit id in superproject' '
	test_when_finished "cleanup_branches super branch-a branch-b" &&
	(
		cd super &&
		git branch --recurse-submodules branch-a &&
		git checkout --recurse-submodules branch-a &&
		git -C sub rev-parse HEAD >expected &&
		# Move the tip of sub:branch-a so that it no longer matches the commit in super:branch-a
		git -C sub checkout branch-a &&
		test_commit -C sub bar &&
		# Create a new branch-b branch with start-point=branch-a
		git branch --recurse-submodules branch-b branch-a &&
		git rev-parse branch-b &&
		git -C sub rev-parse branch-b >actual &&
		# Assert that the commit id of sub:second-branch matches super:branch-a and not sub:branch-a
		test_cmp expected actual
	)
'

test_expect_success 'should not create any branches if branch is not valid for all repos' '
	test_when_finished "cleanup_branches super branch-a" &&
	(
		cd super &&
		git -C sub branch branch-a &&
		test_must_fail git branch --recurse-submodules branch-a 2>actual &&
		test_must_fail git rev-parse branch-a &&

		cat >expected <<-EOF &&
		submodule ${SQ}sub${SQ}: fatal: a branch named ${SQ}branch-a${SQ} already exists
		fatal: submodule ${SQ}sub${SQ}: cannot create branch ${SQ}branch-a${SQ}
		EOF
		test_cmp expected actual
	)
'

test_expect_success 'should create branches if branch exists and --force is given' '
	test_when_finished "cleanup_branches super branch-a" &&
	(
		cd super &&
		git -C sub rev-parse HEAD >expected &&
		test_commit -C sub baz &&
		git -C sub branch branch-a HEAD~1 &&
		git branch --recurse-submodules --force branch-a &&
		git rev-parse branch-a &&
		# assert that sub:branch-a was moved
		git -C sub rev-parse branch-a >actual &&
		test_cmp expected actual
	)
'

test_expect_success 'should create branch when submodule is not in HEAD:.gitmodules' '
	test_when_finished "cleanup_branches super branch-a branch-b branch-c" &&
	(
		cd super &&
		git branch branch-a &&
		git checkout -b branch-b &&
		git submodule add ../sub-upstream sub2 &&
		git -C sub2 submodule update --init &&
		# branch-b now has a committed submodule not in branch-a
		git commit -m "add second submodule" &&
		git checkout branch-a &&
		git branch --recurse-submodules branch-c branch-b &&
		git rev-parse branch-c &&
		git -C sub rev-parse branch-c &&
		git -C second/sub rev-parse branch-c &&
		git checkout --recurse-submodules branch-c &&
		git -C sub2 rev-parse branch-c &&
		git -C sub2/sub-sub rev-parse branch-c
	)
'

test_expect_success 'should set up tracking of local branches with track=always' '
	test_when_finished "cleanup_branches super branch-a" &&
	(
		cd super &&
		git -c branch.autoSetupMerge=always branch --recurse-submodules branch-a main &&
		git -C sub rev-parse main &&
		test "$(git -C sub config branch.branch-a.remote)" = . &&
		test "$(git -C sub config branch.branch-a.merge)" = refs/heads/main
	)
'

test_expect_success 'should set up tracking of local branches with explicit track' '
	test_when_finished "cleanup_branches super branch-a" &&
	(
		cd super &&
		git branch --track --recurse-submodules branch-a main &&
		git -C sub rev-parse main &&
		test "$(git -C sub config branch.branch-a.remote)" = . &&
		test "$(git -C sub config branch.branch-a.merge)" = refs/heads/main
	)
'

test_expect_success 'should not set up unnecessary tracking of local branches' '
	test_when_finished "cleanup_branches super branch-a" &&
	(
		cd super &&
		git branch --recurse-submodules branch-a main &&
		git -C sub rev-parse main &&
		test "$(git -C sub config branch.branch-a.remote)" = "" &&
		test "$(git -C sub config branch.branch-a.merge)" = ""
	)
'

test_expect_success 'should not create branches in inactive submodules' '
	test_when_finished "cleanup_branches super branch-a" &&
	test_config -C super submodule.sub.active false &&
	(
		cd super &&
		git branch --recurse-submodules branch-a &&
		git rev-parse branch-a &&
		test_must_fail git -C sub branch-a
	)
'

test_expect_success 'setup remote-tracking tests' '
	(
		cd super &&
		git branch branch-a &&
		git checkout -b branch-b &&
		git submodule add ../sub-upstream sub2 &&
		# branch-b now has a committed submodule not in branch-a
		git commit -m "add second submodule"
	) &&
	git clone --branch main --recurse-submodules super super-clone &&
	git -C super-clone config submodule.propagateBranches true
'

test_expect_success 'should not create branch when submodule is not in .git/modules' '
	# The cleanup needs to delete sub2 separately because main does not have sub2
	test_when_finished "git -C super-clone/sub2 branch -D branch-b && \
		git -C super-clone/sub2/sub-sub branch -D branch-b && \
		cleanup_branches super-clone branch-a branch-b" &&
	(
		cd super-clone &&
		# This should succeed because super-clone has sub.
		git branch --recurse-submodules branch-a origin/branch-a &&
		# This should fail because super-clone does not have sub2.
		test_must_fail git branch --recurse-submodules branch-b origin/branch-b 2>actual &&
		cat >expected <<-EOF &&
		hint: You may try updating the submodules using ${SQ}git checkout origin/branch-b && git submodule update --init${SQ}
		fatal: submodule ${SQ}sub2${SQ}: unable to find submodule
		EOF
		test_cmp expected actual &&
		test_must_fail git rev-parse branch-b &&
		test_must_fail git -C sub rev-parse branch-b &&
		# User can fix themselves by initializing the submodule
		git checkout origin/branch-b &&
		git submodule update --init --recursive &&
		git branch --recurse-submodules branch-b origin/branch-b
	)
'

test_expect_success 'should set up tracking of remote-tracking branches' '
	test_when_finished "cleanup_branches super-clone branch-a" &&
	(
		cd super-clone &&
		git branch --recurse-submodules branch-a origin/branch-a &&
		test "$(git config branch.branch-a.remote)" = origin &&
		test "$(git config branch.branch-a.merge)" = refs/heads/branch-a &&
		# "origin/branch-a" does not exist for "sub", but it matches the refspec
		# so tracking should be set up
		test "$(git -C sub config branch.branch-a.remote)" = origin &&
		test "$(git -C sub config branch.branch-a.merge)" = refs/heads/branch-a &&
		test "$(git -C sub/sub-sub config branch.branch-a.remote)" = origin &&
		test "$(git -C sub/sub-sub config branch.branch-a.merge)" = refs/heads/branch-a
	)
'

test_expect_success 'should not fail when unable to set up tracking in submodule' '
	test_when_finished "cleanup_branches super-clone branch-a && \
		git -C super-clone remote rename ex-origin origin" &&
	(
		cd super-clone &&
		git remote rename origin ex-origin &&
		git branch --recurse-submodules branch-a ex-origin/branch-a &&
		test "$(git config branch.branch-a.remote)" = ex-origin &&
		test "$(git config branch.branch-a.merge)" = refs/heads/branch-a &&
		test "$(git -C sub config branch.branch-a.remote)" = "" &&
		test "$(git -C sub config branch.branch-a.merge)" = ""
	)
'

test_done
