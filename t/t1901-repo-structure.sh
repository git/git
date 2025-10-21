#!/bin/sh

test_description='test git repo structure'

. ./test-lib.sh

test_expect_success 'empty repository' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		cat >expect <<-\EOF &&
		| Repository structure | Value |
		| -------------------- | ----- |
		| * References         |       |
		|   * Count            |     0 |
		|     * Branches       |     0 |
		|     * Tags           |     0 |
		|     * Remotes        |     0 |
		|     * Others         |     0 |
		|                      |       |
		| * Reachable objects  |       |
		|   * Count            |     0 |
		|     * Commits        |     0 |
		|     * Trees          |     0 |
		|     * Blobs          |     0 |
		|     * Tags           |     0 |
		EOF

		git repo structure >out 2>err &&

		test_cmp expect out &&
		test_line_count = 0 err
	)
'

test_expect_success 'repository with references and objects' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit_bulk 42 &&
		git tag -a foo -m bar &&

		oid="$(git rev-parse HEAD)" &&
		git update-ref refs/remotes/origin/foo "$oid" &&

		# Also creates a commit, tree, and blob.
		git notes add -m foo &&

		cat >expect <<-\EOF &&
		| Repository structure | Value |
		| -------------------- | ----- |
		| * References         |       |
		|   * Count            |     4 |
		|     * Branches       |     1 |
		|     * Tags           |     1 |
		|     * Remotes        |     1 |
		|     * Others         |     1 |
		|                      |       |
		| * Reachable objects  |       |
		|   * Count            |   130 |
		|     * Commits        |    43 |
		|     * Trees          |    43 |
		|     * Blobs          |    43 |
		|     * Tags           |     1 |
		EOF

		git repo structure >out 2>err &&

		test_cmp expect out &&
		test_line_count = 0 err
	)
'

test_done
