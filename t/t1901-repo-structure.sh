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

test_expect_success 'keyvalue and nul format' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit_bulk 42 &&
		git tag -a foo -m bar &&

		cat >expect <<-\EOF &&
		references.branches.count=1
		references.tags.count=1
		references.remotes.count=0
		references.others.count=0
		objects.commits.count=42
		objects.trees.count=42
		objects.blobs.count=42
		objects.tags.count=1
		EOF

		git repo structure --format=keyvalue >out 2>err &&

		test_cmp expect out &&
		test_line_count = 0 err &&

		# Replace key and value delimiters for nul format.
		tr "\n=" "\0\n" <expect >expect_nul &&
		git repo structure --format=nul >out 2>err &&

		test_cmp expect_nul out &&
		test_line_count = 0 err
	)
'

test_expect_success 'progress meter option' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit foo &&

		GIT_PROGRESS_DELAY=0 git repo structure --progress >out 2>err &&

		test_file_not_empty out &&
		test_grep "Counting references: 2, done." err &&
		test_grep "Counting objects: 3, done." err &&

		GIT_PROGRESS_DELAY=0 git repo structure --no-progress >out 2>err &&

		test_file_not_empty out &&
		test_line_count = 0 err
	)
'

test_done
