#!/bin/sh

test_description='test git repo structure'

. ./test-lib.sh

object_type_disk_usage() {
	git cat-file --batch-check='%(objectsize:disk)' --batch-all-objects \
		--filter=object:type=$1 | awk '{ sum += $1 } END { print sum }'
}

strip_object_disk_usage() {
	awk '
		/^\|   \* Disk size/ { skip=1; next }
		skip && /^\|     \* / { next }
		skip && !/^\|     \* / { skip=0 }
		{ print }
	' $1
}

test_expect_success 'empty repository' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		cat >expect <<-\EOF &&
		| Repository structure | Value  |
		| -------------------- | ------ |
		| * References         |        |
		|   * Count            |    0   |
		|     * Branches       |    0   |
		|     * Tags           |    0   |
		|     * Remotes        |    0   |
		|     * Others         |    0   |
		|                      |        |
		| * Reachable objects  |        |
		|   * Count            |    0   |
		|     * Commits        |    0   |
		|     * Trees          |    0   |
		|     * Blobs          |    0   |
		|     * Tags           |    0   |
		|   * Inflated size    |    0 B |
		|     * Commits        |    0 B |
		|     * Trees          |    0 B |
		|     * Blobs          |    0 B |
		|     * Tags           |    0 B |
		|   * Disk size        |    0 B |
		|     * Commits        |    0 B |
		|     * Trees          |    0 B |
		|     * Blobs          |    0 B |
		|     * Tags           |    0 B |
		EOF

		git repo structure >out 2>err &&

		test_cmp expect out &&
		test_line_count = 0 err
	)
'

test_expect_success SHA1 'repository with references and objects' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit_bulk 1005 &&
		git tag -a foo -m bar &&

		oid="$(git rev-parse HEAD)" &&
		git update-ref refs/remotes/origin/foo "$oid" &&

		# Also creates a commit, tree, and blob.
		git notes add -m foo &&

		cat >expect <<-\EOF &&
		| Repository structure | Value      |
		| -------------------- | ---------- |
		| * References         |            |
		|   * Count            |      4     |
		|     * Branches       |      1     |
		|     * Tags           |      1     |
		|     * Remotes        |      1     |
		|     * Others         |      1     |
		|                      |            |
		| * Reachable objects  |            |
		|   * Count            |   3.02 k   |
		|     * Commits        |   1.01 k   |
		|     * Trees          |   1.01 k   |
		|     * Blobs          |   1.01 k   |
		|     * Tags           |      1     |
		|   * Inflated size    |  16.03 MiB |
		|     * Commits        | 217.92 KiB |
		|     * Trees          |  15.81 MiB |
		|     * Blobs          |  11.68 KiB |
		|     * Tags           |    132 B   |
		EOF

		git repo structure >out.raw 2>err &&

		# Skip object disk sizes due to platform variance.
		strip_object_disk_usage out.raw >out &&

		test_cmp expect out &&
		test_line_count = 0 err
	)
'

test_expect_success SHA1 'keyvalue and nul format' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit_bulk 42 &&
		git tag -a foo -m bar &&

		cat >expect <<-EOF &&
		references.branches.count=1
		references.tags.count=1
		references.remotes.count=0
		references.others.count=0
		objects.commits.count=42
		objects.trees.count=42
		objects.blobs.count=42
		objects.tags.count=1
		objects.commits.inflated_size=9225
		objects.trees.inflated_size=28554
		objects.blobs.inflated_size=453
		objects.tags.inflated_size=132
		objects.commits.disk_size=$(object_type_disk_usage commit)
		objects.trees.disk_size=$(object_type_disk_usage tree)
		objects.blobs.disk_size=$(object_type_disk_usage blob)
		objects.tags.disk_size=$(object_type_disk_usage tag)
		EOF

		git repo structure --format=keyvalue >out 2>err &&

		test_cmp expect out &&
		test_line_count = 0 err &&

		# Replace key and value delimiters for nul format.
		tr "\n=" "\0\n" <expect >expect_nul &&
		git repo structure --format=nul >out 2>err &&

		test_cmp expect_nul out &&
		test_line_count = 0 err &&

		# "-z", as a synonym to "--format=nul", participates in the
		# usual "last one wins" rule.
		git repo structure --format=table -z >out 2>err &&

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
