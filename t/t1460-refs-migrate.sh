#!/bin/sh

test_description='migration of ref storage backends'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_migration () {
	git -C "$1" for-each-ref --include-root-refs \
		--format='%(refname) %(objectname) %(symref)' >expect &&
	git -C "$1" refs migrate --ref-format="$2" &&
	git -C "$1" for-each-ref --include-root-refs \
		--format='%(refname) %(objectname) %(symref)' >actual &&
	test_cmp expect actual &&

	git -C "$1" rev-parse --show-ref-format >actual &&
	echo "$2" >expect &&
	test_cmp expect actual
}

test_expect_success 'setup' '
	rm -rf .git &&
	# The migration does not yet support reflogs.
	git config --global core.logAllRefUpdates false
'

test_expect_success "superfluous arguments" '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_must_fail git -C repo refs migrate foo 2>err &&
	test_grep "takes no arguments" err
'

test_expect_success "missing ref storage format" '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_must_fail git -C repo refs migrate 2>err &&
	test_grep "needs ${SQ}--ref-format=<format>${SQ}" err
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

		test_expect_success "$from_format -> $to_format: migration with reflog fails" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			test_config -C repo core.logAllRefUpdates true &&
			test_commit -C repo logged &&
			test_must_fail git -C repo refs migrate \
				--ref-format=$to_format 2>err &&
			cat >expect <<-EOF &&
			error: migrating reflogs is not supported yet
			EOF
			test_cmp expect err
		'

		test_expect_success "$from_format -> $to_format: migration with worktree fails" '
			test_when_finished "rm -rf repo" &&
			git init --ref-format=$from_format repo &&
			git -C repo worktree add wt &&
			test_must_fail git -C repo refs migrate \
				--ref-format=$to_format 2>err &&
			cat >expect <<-EOF &&
			error: migrating repositories with worktrees is not supported yet
			EOF
			test_cmp expect err
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
			test_migration repo "$to_format" &&
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
	done
done

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
	test_path_is_file repo/.git/refs/heads/main
'

test_done
