#!/bin/sh

test_description='migration of ref storage backends'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

print_all_reflog_entries () {
	repo=$1 &&
	test-tool -C "$repo" ref-store main for-each-reflog >reflogs &&
	while read reflog
	do
		echo "REFLOG: $reflog" &&
		test-tool -C "$repo" ref-store main for-each-reflog-ent "$reflog" ||
		return 1
	done <reflogs
}

# Migrate the provided repository from one format to the other and
# verify that the references and logs are migrated over correctly.
# Usage: test_migration <repo> <format> [<skip_reflog_verify> [<options...>]]
#   <repo> is the relative path to the repo to be migrated.
#   <format> is the ref format to be migrated to.
#   <skip_reflog_verify> (default: false) whether to skip reflog verification.
#   <options...> are other options be passed directly to 'git refs migrate'.
test_migration () {
	repo=$1 &&
	format=$2 &&
	shift 2 &&
	skip_reflog_verify=false &&
	if test $# -ge 1
	then
		skip_reflog_verify=$1
		shift
	fi &&
	git -C "$repo" for-each-ref --include-root-refs \
		--format='%(refname) %(objectname) %(symref)' >expect &&
	if ! $skip_reflog_verify
	then
		print_all_reflog_entries "$repo" >expect_logs
	fi &&

	git -C "$repo" refs migrate --ref-format="$format" "$@" &&

	git -C "$repo" for-each-ref --include-root-refs \
		--format='%(refname) %(objectname) %(symref)' >actual &&
	test_cmp expect actual &&
	if ! $skip_reflog_verify
	then
		print_all_reflog_entries "$repo" >actual_logs &&
		test_cmp expect_logs actual_logs
	fi &&

	git -C "$repo" rev-parse --show-ref-format >actual &&
	echo "$format" >expect &&
	test_cmp expect actual
}

test_expect_success 'setup' '
	rm -rf .git
'

test_expect_success "superfluous arguments" '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_must_fail git -C repo refs migrate foo 2>err &&
	cat >expect <<-EOF &&
	usage: too many arguments
	EOF
	test_cmp expect err
'

test_expect_success "missing ref storage format" '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_must_fail git -C repo refs migrate 2>err &&
	cat >expect <<-EOF &&
	usage: missing --ref-format=<format>
	EOF
	test_cmp expect err
'

test_expect_success "unknown ref storage format" '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_must_fail git -C repo refs migrate \
		--ref-format=unknown 2>err &&
	cat >expect <<-EOF &&
	error: unknown ref storage format ${SQ}unknown${SQ}
	EOF
	test_cmp expect err
'

ref_formats="files reftable"
for from_format in $ref_formats
do
	for to_format in $ref_formats
	do
		if test "$from_format" = "$to_format"
		then
			continue
		fi

		test_expect_success "$from_format: migration to same format fails" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			test_must_fail git -C repo refs migrate \
				--ref-format=$from_format 2>err &&
			cat >expect <<-EOF &&
			error: repository already uses ${SQ}$from_format${SQ} format
			EOF
			test_cmp expect err
		'

		test_expect_success "$from_format -> $to_format: migration with worktree" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			test_commit -C repo initial &&
			git -C repo worktree add wt &&

			# Create some refs and reflogs in both worktrees
			test_commit -C repo second &&
			git -C repo update-ref refs/heads/from-main HEAD &&
			git -C repo/wt checkout -b wt-branch &&
			test_commit -C repo/wt wt-commit &&
			git -C repo/wt update-ref refs/bisect/wt-ref HEAD &&

			# Capture refs from both worktrees before migration
			git -C repo for-each-ref --include-root-refs \
				--format="%(refname) %(objectname) %(symref)" >expect-main &&
			git -C repo/wt for-each-ref --include-root-refs \
				--format="%(refname) %(objectname) %(symref)" >expect-wt &&

			# Perform migration
			git -C repo refs migrate --ref-format=$to_format &&

			# Verify refs in both worktrees after migration
			git -C repo for-each-ref --include-root-refs \
				--format="%(refname) %(objectname) %(symref)" >actual-main &&
			git -C repo/wt for-each-ref --include-root-refs \
				--format="%(refname) %(objectname) %(symref)" >actual-wt &&
			test_cmp expect-main actual-main &&
			test_cmp expect-wt actual-wt &&

			# Verify repository format changed
			git -C repo rev-parse --show-ref-format >actual &&
			echo "$to_format" >expect &&
			test_cmp expect actual
		'

		test_expect_success "$from_format -> $to_format: unborn HEAD" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			test_migration repo "$to_format"
		'

		test_expect_success "$from_format -> $to_format: single ref" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			test_commit -C repo initial &&
			test_migration repo "$to_format"
		'

		test_expect_success "$from_format -> $to_format: bare repository" '
			test_when_finished "rm -rf repo repo.git" &&
			git init --ref-format=$from_format repo &&
			test_commit -C repo initial &&
			git clone --ref-format=$from_format --mirror repo repo.git &&
			test_migration repo.git "$to_format"
		'

		test_expect_success "$from_format -> $to_format: dangling symref" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			test_commit -C repo initial &&
			git -C repo symbolic-ref BROKEN_HEAD refs/heads/nonexistent &&
			test_migration repo "$to_format" &&
			echo refs/heads/nonexistent >expect &&
			git -C repo symbolic-ref BROKEN_HEAD >actual &&
			test_cmp expect actual
		'

		test_expect_success "$from_format -> $to_format: broken ref" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			test_commit -C repo initial &&
			test-tool -C repo ref-store main update-ref "" refs/heads/broken \
				"$(test_oid 001)" "$ZERO_OID" REF_SKIP_CREATE_REFLOG,REF_SKIP_OID_VERIFICATION &&
			test_migration repo "$to_format" true &&
			test_oid 001 >expect &&
			git -C repo rev-parse refs/heads/broken >actual &&
			test_cmp expect actual
		'

		test_expect_success "$from_format -> $to_format: pseudo-refs" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			test_commit -C repo initial &&
			git -C repo update-ref FOO_HEAD HEAD &&
			test_migration repo "$to_format"
		'

		test_expect_success "$from_format -> $to_format: special refs are left alone" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			test_commit -C repo initial &&
			git -C repo rev-parse HEAD >repo/.git/MERGE_HEAD &&
			git -C repo rev-parse MERGE_HEAD &&
			test_migration repo "$to_format" &&
			test_path_is_file repo/.git/MERGE_HEAD
		'

		test_expect_success "$from_format -> $to_format: a bunch of refs" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&

			test_commit -C repo initial &&
			cat >input <<-EOF &&
			create FOO_HEAD HEAD
			create refs/heads/branch-1 HEAD
			create refs/heads/branch-2 HEAD
			create refs/heads/branch-3 HEAD
			create refs/heads/branch-4 HEAD
			create refs/tags/tag-1 HEAD
			create refs/tags/tag-2 HEAD
			EOF
			git -C repo update-ref --stdin <input &&
			test_migration repo "$to_format"
		'

		test_expect_success "$from_format -> $to_format: dry-run migration does not modify repository" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			test_commit -C repo initial &&
			git -C repo refs migrate --dry-run \
				--ref-format=$to_format >output &&
			grep "Finished dry-run migration of refs" output &&
			test_path_is_dir repo/.git/ref_migration.* &&
			echo $from_format >expect &&
			git -C repo rev-parse --show-ref-format >actual &&
			test_cmp expect actual
		'

		test_expect_success "$from_format -> $to_format: reflogs of symrefs with target deleted" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			test_commit -C repo initial &&
			git -C repo branch branch-1 HEAD &&
			git -C repo symbolic-ref refs/heads/symref refs/heads/branch-1 &&
			cat >input <<-EOF &&
			delete refs/heads/branch-1
			EOF
			git -C repo update-ref --stdin <input &&
			test_migration repo "$to_format"
		'

		test_expect_success "$from_format -> $to_format: reflogs order is retained" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			test_commit --date "100005000 +0700" --no-tag -C repo initial &&
			test_commit --date "100003000 +0700" --no-tag -C repo second &&
			test_migration repo "$to_format"
		'

		test_expect_success "$from_format -> $to_format: stash is retained" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			(
				cd repo &&
				test_commit initial A &&
				echo foo >A &&
				git stash push &&
				echo bar >A &&
				git stash push &&
				git stash list >expect.reflog &&
				test_migration . "$to_format" &&
				git stash list >actual.reflog &&
				test_cmp expect.reflog actual.reflog
			)
		'

		test_expect_success "$from_format -> $to_format: skip reflog with --skip-reflog" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			test_commit -C repo initial &&
			# we see that the repository contains reflogs.
			git -C repo reflog --all >reflogs &&
			test_line_count = 2 reflogs &&
			test_migration repo "$to_format" true --no-reflog &&
			# there should be no reflogs post migration.
			git -C repo reflog --all >reflogs &&
			test_must_be_empty reflogs
		'
	done
done

test_expect_success 'multiple reftable blocks with multiple entries' '
	test_when_finished "rm -rf repo" &&
	git init --ref-format=files repo &&
	test_commit -C repo first &&
	printf "create refs/heads/ref-%d HEAD\n" $(test_seq 5000) >stdin &&
	git -C repo update-ref --stdin <stdin &&
	test_commit -C repo second &&
	printf "update refs/heads/ref-%d HEAD\n" $(test_seq 3000) >stdin &&
	git -C repo update-ref --stdin <stdin &&
	test_migration repo reftable true
'

test_expect_success 'migrating from files format deletes backend files' '
	test_when_finished "rm -rf repo" &&
	git init --ref-format=files repo &&
	test_commit -C repo first &&
	git -C repo pack-refs --all &&
	test_commit -C repo second &&
	git -C repo update-ref ORIG_HEAD HEAD &&
	git -C repo rev-parse HEAD >repo/.git/FETCH_HEAD &&

	test_path_is_file repo/.git/HEAD &&
	test_path_is_file repo/.git/ORIG_HEAD &&
	test_path_is_file repo/.git/refs/heads/main &&
	test_path_is_file repo/.git/packed-refs &&

	test_migration repo reftable &&

	echo "ref: refs/heads/.invalid" >expect &&
	test_cmp expect repo/.git/HEAD &&
	echo "this repository uses the reftable format" >expect &&
	test_cmp expect repo/.git/refs/heads &&
	test_path_is_file repo/.git/FETCH_HEAD &&
	test_path_is_missing repo/.git/ORIG_HEAD &&
	test_path_is_missing repo/.git/refs/heads/main &&
	test_path_is_missing repo/.git/logs &&
	test_path_is_missing repo/.git/packed-refs
'

test_expect_success 'migrating from reftable format deletes backend files' '
	test_when_finished "rm -rf repo" &&
	git init --ref-format=reftable repo &&
	test_commit -C repo first &&

	test_path_is_dir repo/.git/reftable &&
	test_migration repo files &&

	test_path_is_missing repo/.git/reftable &&
	echo "ref: refs/heads/main" >expect &&
	test_cmp expect repo/.git/HEAD &&
	test_path_is_file repo/.git/packed-refs
'

test_expect_success 'files -> reftable: migration with multiple worktrees' '
	test_when_finished "rm -rf repo" &&
	git init --ref-format=files repo &&
	test_commit -C repo initial &&
	git -C repo worktree add wt1 &&
	git -C repo worktree add wt2 &&

	# Create unique refs in each worktree
	test_commit -C repo main-commit &&
	test_commit -C repo/wt1 wt1-commit &&
	test_commit -C repo/wt2 wt2-commit &&
	git -C repo update-ref refs/bisect/main-bisect HEAD &&
	git -C repo/wt1 update-ref refs/bisect/wt1-bisect HEAD &&
	git -C repo/wt2 update-ref refs/bisect/wt2-bisect HEAD &&

	# Capture state before migration
	git -C repo for-each-ref --include-root-refs \
		--format="%(refname) %(objectname)" | sort >expect-main &&
	git -C repo/wt1 for-each-ref --include-root-refs \
		--format="%(refname) %(objectname)" | sort >expect-wt1 &&
	git -C repo/wt2 for-each-ref --include-root-refs \
		--format="%(refname) %(objectname)" | sort >expect-wt2 &&

	# Migrate
	git -C repo refs migrate --ref-format=reftable &&

	# Verify all worktrees still work
	git -C repo for-each-ref --include-root-refs \
		--format="%(refname) %(objectname)" | sort >actual-main &&
	git -C repo/wt1 for-each-ref --include-root-refs \
		--format="%(refname) %(objectname)" | sort >actual-wt1 &&
	git -C repo/wt2 for-each-ref --include-root-refs \
		--format="%(refname) %(objectname)" | sort >actual-wt2 &&
	test_cmp expect-main actual-main &&
	test_cmp expect-wt1 actual-wt1 &&
	test_cmp expect-wt2 actual-wt2 &&

	# Verify format changed
	git -C repo rev-parse --show-ref-format >actual &&
	echo "reftable" >expect &&
	test_cmp expect actual &&

	# Verify operations still work in all worktrees
	test_commit -C repo post-migrate-main &&
	test_commit -C repo/wt1 post-migrate-wt1 &&
	test_commit -C repo/wt2 post-migrate-wt2
'

test_expect_success 'files -> reftable: dry-run with worktrees' '
	test_when_finished "rm -rf repo" &&
	git init --ref-format=files repo &&
	test_commit -C repo initial &&
	git -C repo worktree add wt &&

	git -C repo refs migrate --ref-format=reftable --dry-run >output &&
	grep "Finished dry-run migration" output &&
	grep "2 worktree" output &&

	# Format should not have changed
	git -C repo rev-parse --show-ref-format >actual &&
	echo "files" >expect &&
	test_cmp expect actual &&

	# Files backend should still be present
	test_path_is_file repo/.git/refs/heads/main
'

test_expect_success 'reftable -> files: migration with worktrees and per-worktree refs' '
	test_when_finished "rm -rf repo" &&
	git init --ref-format=reftable repo &&
	test_commit -C repo initial &&
	git -C repo worktree add wt &&

	# Create various types of per-worktree refs
	test_commit -C repo main-work &&
	git -C repo update-ref refs/bisect/bad HEAD &&
	git -C repo update-ref refs/rewritten/main HEAD &&
	git -C repo update-ref refs/worktree/custom HEAD &&

	test_commit -C repo/wt wt-work &&
	git -C repo/wt update-ref refs/bisect/good HEAD &&
	git -C repo/wt update-ref refs/rewritten/wt HEAD &&
	git -C repo/wt update-ref refs/worktree/wt-custom HEAD &&

	# Capture all refs including per-worktree ones
	git -C repo for-each-ref --include-root-refs \
		--format="%(refname) %(objectname)" | sort >expect-main &&
	git -C repo/wt for-each-ref --include-root-refs \
		--format="%(refname) %(objectname)" | sort >expect-wt &&

	# Migrate back to files
	git -C repo refs migrate --ref-format=files &&

	# Verify per-worktree refs are still separate
	git -C repo for-each-ref --include-root-refs \
		--format="%(refname) %(objectname)" | sort >actual-main &&
	git -C repo/wt for-each-ref --include-root-refs \
		--format="%(refname) %(objectname)" | sort >actual-wt &&
	test_cmp expect-main actual-main &&
	test_cmp expect-wt actual-wt &&

	# Verify physical separation of per-worktree refs
	test_path_is_file repo/.git/refs/bisect/bad &&
	test_path_is_file repo/.git/worktrees/wt/refs/bisect/good &&
	test_path_is_missing repo/.git/refs/bisect/good &&
	test_path_is_missing repo/.git/worktrees/wt/refs/bisect/bad
'

test_expect_success 'bare repository with worktrees: bidirectional migration' '
	test_when_finished "rm -rf bare-repo worktrees" &&

	# Create a bare repository
	git init --bare --ref-format=files bare-repo &&

	# Add worktrees to the bare repository
	mkdir worktrees &&
	git -C bare-repo worktree add ../worktrees/main &&
	git -C bare-repo worktree add ../worktrees/feature &&

	# Create initial commits and refs in main worktree
	test_commit -C worktrees/main initial &&
	git -C worktrees/main update-ref refs/heads/main HEAD &&
	git -C worktrees/main update-ref refs/bisect/main-bad HEAD &&
	git -C worktrees/main update-ref refs/worktree/main-custom HEAD &&

	# Create commits and refs in feature worktree
	test_commit -C worktrees/feature feature-work &&
	git -C worktrees/feature update-ref refs/bisect/feature-bad HEAD &&
	git -C worktrees/feature update-ref refs/worktree/feature-custom HEAD &&

	# Capture all refs before migration
	git -C worktrees/main for-each-ref --include-root-refs \
		--format="%(refname) %(objectname)" | sort >expect-main &&
	git -C worktrees/feature for-each-ref --include-root-refs \
		--format="%(refname) %(objectname)" | sort >expect-feature &&

	# Migrate bare repo to reftable
	git -C bare-repo refs migrate --ref-format=reftable &&

	# Verify format changed
	git -C bare-repo rev-parse --show-ref-format >actual &&
	echo "reftable" >expect-format &&
	test_cmp expect-format actual &&

	# Verify all refs still exist and are correct
	git -C worktrees/main for-each-ref --include-root-refs \
		--format="%(refname) %(objectname)" | sort >actual-main &&
	git -C worktrees/feature for-each-ref --include-root-refs \
		--format="%(refname) %(objectname)" | sort >actual-feature &&
	test_cmp expect-main actual-main &&
	test_cmp expect-feature actual-feature &&

	# Migrate back to files
	git -C bare-repo refs migrate --ref-format=files &&

	# Verify format changed back
	git -C bare-repo rev-parse --show-ref-format >actual &&
	echo "files" >expect-format &&
	test_cmp expect-format actual &&

	# Verify all refs still exist and are correct after round-trip
	git -C worktrees/main for-each-ref --include-root-refs \
		--format="%(refname) %(objectname)" | sort >actual-main &&
	git -C worktrees/feature for-each-ref --include-root-refs \
		--format="%(refname) %(objectname)" | sort >actual-feature &&
	test_cmp expect-main actual-main &&
	test_cmp expect-feature actual-feature &&

	# Verify physical separation of per-worktree refs
	test_path_is_file bare-repo/worktrees/main/refs/bisect/main-bad &&
	test_path_is_file bare-repo/worktrees/feature/refs/bisect/feature-bad &&
	test_path_is_missing bare-repo/worktrees/main/refs/bisect/feature-bad &&
	test_path_is_missing bare-repo/worktrees/feature/refs/bisect/main-bad
'

test_expect_success SANITY 'files -> reftable: migration fails with read-only .git' '
	test_when_finished "chmod -R u+w read-only-git" &&
	git init --ref-format=files read-only-git &&
	test_commit -C read-only-git initial &&
	chmod -R a-w read-only-git/.git &&
	test_must_fail git -C read-only-git refs migrate --ref-format=reftable 2>err &&
	grep -i "permission denied\|read-only" err
'

test_expect_success SANITY 'files -> reftable: read-only refs directory prevents backup' '
	test_when_finished "chmod -R u+w read-only-refs" &&
	git init --ref-format=files read-only-refs &&
	test_commit -C read-only-refs initial &&
	chmod a-w read-only-refs/.git/refs &&
	test_must_fail git -C read-only-refs refs migrate --ref-format=reftable 2>err &&
	chmod u+w read-only-refs/.git/refs &&
	grep -i "could not\|permission denied" err
'

test_expect_success 'files -> reftable: git status works in all worktrees after migration' '
	test_when_finished "rm -rf repo" &&
	git init --ref-format=files repo &&
	test_commit -C repo initial &&
	git -C repo worktree add wt1 &&
	git -C repo worktree add wt2 &&

	# Make some commits in each worktree
	test_commit -C repo main-work &&
	test_commit -C repo/wt1 wt1-work &&
	test_commit -C repo/wt2 wt2-work &&

	# Verify status works before migration using -C
	git -C repo status &&
	git -C repo/wt1 status &&
	git -C repo/wt2 status &&

	# Verify status works before migration by cd-ing into worktree
	(cd repo && git status) &&
	(cd repo/wt1 && git status) &&
	(cd repo/wt2 && git status) &&

	# Migrate to reftable
	git -C repo refs migrate --ref-format=reftable &&

	# Verify status still works after migration using -C
	git -C repo status &&
	git -C repo/wt1 status &&
	git -C repo/wt2 status &&

	# Verify status works after migration by cd-ing into worktree
	(cd repo && git status) &&
	(cd repo/wt1 && git status) &&
	(cd repo/wt2 && git status) &&

	# Verify other common commands work in all worktrees
	git -C repo log --oneline &&
	git -C repo/wt1 log --oneline &&
	git -C repo/wt2 log --oneline &&

	git -C repo branch &&
	git -C repo/wt1 branch &&
	git -C repo/wt2 branch &&

	# Migrate back to files
	git -C repo refs migrate --ref-format=files &&

	# Verify status still works after migrating back
	git -C repo status &&
	git -C repo/wt1 status &&
	git -C repo/wt2 status &&

	(cd repo && git status) &&
	(cd repo/wt1 && git status) &&
	(cd repo/wt2 && git status)
'

test_expect_success 'files -> reftable: migration fails from inside linked worktree' '
	test_when_finished "rm -rf from-wt-bare.git from-wt-trees" &&

	# Create a bare repo with worktrees
	git init --bare --ref-format=files from-wt-bare.git &&

	# Add two worktrees
	mkdir from-wt-trees &&
	git -C from-wt-bare.git worktree add ../from-wt-trees/wt1 &&
	git -C from-wt-bare.git worktree add ../from-wt-trees/wt2 &&

	# Create commits in first worktree
	test_commit -C from-wt-trees/wt1 initial &&
	test_commit -C from-wt-trees/wt1 second &&

	# Migration from inside a linked worktree should fail with helpful error
	(
		cd from-wt-trees/wt1 &&
		test_must_fail git refs migrate --ref-format=reftable 2>err
	) &&
	grep "migration must be run from the main worktree" from-wt-trees/wt1/err &&

	# Verify repository is not corrupted - refs format should still be files
	git -C from-wt-bare.git rev-parse --show-ref-format >actual-format &&
	echo "files" >expect-format &&
	test_cmp expect-format actual-format &&

	# Verify git status still works in the worktree
	git -C from-wt-trees/wt1 status &&
	(cd from-wt-trees/wt1 && git status) &&

	# Verify migration succeeds when run from the main repository
	git -C from-wt-bare.git refs migrate --ref-format=reftable &&

	# Verify migration actually happened
	git -C from-wt-bare.git rev-parse --show-ref-format >actual-format-after &&
	echo "reftable" >expect-format-after &&
	test_cmp expect-format-after actual-format-after &&

	# Verify worktree still works after successful migration
	git -C from-wt-trees/wt1 status &&
	(cd from-wt-trees/wt1 && git status)
'

test_expect_success 'files -> reftable: migration with uncommitted changes in worktrees' '
	test_when_finished "rm -rf dirty-wt-repo dirty-wt" &&

	# Create repo with initial commit
	git init --ref-format=files dirty-wt-repo &&
	test_commit -C dirty-wt-repo initial &&

	# Create worktree and make a commit there so it has tracked files
	git -C dirty-wt-repo worktree add ../dirty-wt &&
	test_commit -C dirty-wt base &&

	# Create uncommitted changes in worktree:
	# 1. Untracked file
	echo "untracked content" >dirty-wt/untracked.txt &&

	# 2. Modified tracked file (not staged)
	echo "modified" >>dirty-wt/base.t &&

	# 3. Staged new file
	echo "staged new content" >dirty-wt/staged-new.txt &&
	git -C dirty-wt add staged-new.txt &&

	# 4. Staged modification to tracked file
	echo "staged modification" >>dirty-wt/initial.t &&
	git -C dirty-wt add initial.t &&

	# 5. File with both staged AND unstaged changes
	echo "staged change" >dirty-wt/both.txt &&
	git -C dirty-wt add both.txt &&
	echo "unstaged change" >>dirty-wt/both.txt &&

	# Record status before migration
	git -C dirty-wt status --porcelain >status-before &&

	# Verify status works before migration
	git -C dirty-wt status &&
	(cd dirty-wt && git status) &&

	# Migrate from main worktree
	git -C dirty-wt-repo refs migrate --ref-format=reftable &&

	# Verify migration succeeded
	git -C dirty-wt-repo rev-parse --show-ref-format >actual &&
	echo "reftable" >expect &&
	test_cmp expect actual &&

	# Verify status still works after migration
	git -C dirty-wt status &&
	(cd dirty-wt && git status) &&

	# Verify all uncommitted changes are preserved exactly
	git -C dirty-wt status --porcelain >status-after &&
	test_cmp status-before status-after &&

	# Verify file contents are preserved
	test "$(cat dirty-wt/untracked.txt)" = "untracked content" &&
	grep "modified" dirty-wt/base.t &&
	test "$(cat dirty-wt/staged-new.txt)" = "staged new content" &&
	grep "staged modification" dirty-wt/initial.t &&
	test "$(cat dirty-wt/both.txt)" = "staged change
unstaged change" &&

	# Verify all 5 types of changes are still present in status
	test_line_count = 5 status-after
'

test_expect_success 'files -> reftable: migration with prunable worktree' '
	test_when_finished "rm -rf prunable-repo" &&

	# Create repo with worktree, then delete the worktree directory
	git init --ref-format=files prunable-repo &&
	test_commit -C prunable-repo initial &&
	git -C prunable-repo worktree add ../prunable-wt &&
	rm -rf ../prunable-wt &&

	# Migration should still succeed
	git -C prunable-repo refs migrate --ref-format=reftable &&

	# Verify migration succeeded
	git -C prunable-repo rev-parse --show-ref-format >actual &&
	echo "reftable" >expect &&
	test_cmp expect actual &&

	# Verify worktree is marked as prunable but metadata exists
	git -C prunable-repo worktree list --porcelain >list &&
	grep "prunable" list
'

test_expect_success 'files -> reftable: migration works from main worktree .git directory' '
	test_when_finished "rm -rf from-gitdir-repo" &&

	git init --ref-format=files from-gitdir-repo &&
	test_commit -C from-gitdir-repo initial &&

	# Verify status works before migration
	(cd from-gitdir-repo && git status) &&

	# Run migration from inside .git directory
	(
		cd from-gitdir-repo/.git &&
		git refs migrate --ref-format=reftable
	) &&

	# Verify migration succeeded
	git -C from-gitdir-repo rev-parse --show-ref-format >actual &&
	echo "reftable" >expect &&
	test_cmp expect actual &&

	# Verify repo still works
	git -C from-gitdir-repo status &&
	(cd from-gitdir-repo && git status)
'

test_done
