#!/bin/sh

test_description='Test reftable backend consistency check'

GIT_TEST_DEFAULT_REF_FORMAT=reftable
export GIT_TEST_DEFAULT_REF_FORMAT

. ./test-lib.sh

test_expect_success "no errors reported on a well formed repository" '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		git commit --allow-empty -m initial &&

		for i in $(test_seq 20)
		do
			git update-ref refs/heads/branch-$i HEAD || return 1
		done &&

		# The repository should end up with multiple tables.
		test_line_count -gt 1 .git/reftable/tables.list &&

		git refs verify 2>err &&
		test_must_be_empty err
	)
'

for TABLE_NAME in "foo-bar-e4d12d59.ref" \
	"0x00000000zzzz-0x00000000zzzz-e4d12d59.ref" \
	"0x000000000001-0x000000000002-e4d12d59.abc" \
	"0x000000000001-0x000000000002-e4d12d59.refabc"; do
	test_expect_success "table name $TABLE_NAME should be checked" '
		test_when_finished "rm -rf repo" &&
		git init repo &&
		(
			cd repo &&
			git commit --allow-empty -m initial &&

			git refs verify 2>err &&
			test_must_be_empty err &&

			EXISTING_TABLE=$(head -n1 .git/reftable/tables.list) &&
			mv ".git/reftable/$EXISTING_TABLE" ".git/reftable/$TABLE_NAME" &&
			sed "s/${EXISTING_TABLE}/${TABLE_NAME}/g" .git/reftable/tables.list > tables.list &&
			mv tables.list .git/reftable/tables.list &&

			git refs verify 2>err &&
			cat >expect <<-EOF &&
			warning: ${TABLE_NAME}: badReftableTableName: invalid reftable table name
			EOF
			test_cmp expect err
		)
	'
done

test_expect_success 'worktree stacks can be verified' '
	test_when_finished "rm -rf repo worktree" &&
	git init repo &&
	test_commit -C repo initial &&
	git -C repo worktree add ../worktree &&

	git -C worktree refs verify 2>err &&
	test_must_be_empty err &&

	REFTABLE_DIR=$(git -C worktree rev-parse --git-dir)/reftable &&
	EXISTING_TABLE=$(head -n1 "$REFTABLE_DIR/tables.list") &&
	mv "$REFTABLE_DIR/$EXISTING_TABLE" "$REFTABLE_DIR/broken.ref" &&

	for d in repo worktree
	do
		echo "broken.ref" >"$REFTABLE_DIR/tables.list" &&
		git -C "$d" refs verify 2>err &&
		cat >expect <<-EOF &&
		warning: broken.ref: badReftableTableName: invalid reftable table name
		EOF
		test_cmp expect err &&

		echo garbage >"$REFTABLE_DIR/tables.list" &&
		test_must_fail git -C "$d" refs verify 2>err &&
		cat >expect <<-EOF &&
		error: reftable stack for worktree ${SQ}worktree${SQ} is broken
		EOF
		test_cmp expect err || return 1

	done
'

test_expect_success 'invalid symref gets reported' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_commit -C repo initial &&
	git -C repo symbolic-ref refs/heads/symref garbage &&
	test_must_fail git -C repo refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/heads/symref: badReferentName: points to invalid refname ${SQ}garbage${SQ}
	EOF
	test_cmp expect err
'

test_done
