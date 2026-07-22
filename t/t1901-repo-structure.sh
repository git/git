#!/bin/sh

test_description='test git repo structure'

. ./test-lib.sh

object_type_disk_usage() {
	disk_usage_opt="--disk-usage"

	if test "$2" = "true"
	then
		disk_usage_opt="--disk-usage=human"
	fi

	if test "$1" = "all"
	then
		git rev-list --all --objects $disk_usage_opt
	else
		git rev-list --all --objects $disk_usage_opt \
			--filter=object:type=$1 --filter-provided-objects
	fi
}

test_expect_success 'empty repository' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		cat >expect <<-\EOF &&
		| Repository structure      | Value  |
		| ------------------------- | ------ |
		| * References              |        |
		|   * Count                 |    0   |
		|     * Branches            |    0   |
		|     * Tags                |    0   |
		|     * Remotes             |    0   |
		|     * Others              |    0   |
		|                           |        |
		| * Reachable objects       |        |
		|   * Count                 |    0   |
		|     * Commits             |    0   |
		|     * Trees               |    0   |
		|     * Blobs               |    0   |
		|     * Tags                |    0   |
		|   * Inflated size         |    0 B |
		|     * Commits             |    0 B |
		|     * Trees               |    0 B |
		|     * Blobs               |    0 B |
		|     * Tags                |    0 B |
		|   * Disk size             |    0 B |
		|     * Commits             |    0 B |
		|     * Trees               |    0 B |
		|     * Blobs               |    0 B |
		|     * Tags                |    0 B |
		|                           |        |
		| * Largest objects         |        |
		|   * Commits               |        |
		|     * Maximum size        |    0 B |
		|     * Maximum parents     |    0   |
		|   * Trees                 |        |
		|     * Maximum size        |    0 B |
		|     * Maximum entries     |    0   |
		|   * Blobs                 |        |
		|     * Maximum size        |    0 B |
		|   * Tags                  |        |
		|     * Maximum size        |    0 B |
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

		# The tags disk size is handled specially due to the
		# git-rev-list(1) --disk-usage=human option printing the full
		# "byte/bytes" unit string instead of just "B".
		cat >expect <<-EOF &&
		| Repository structure      | Value      |
		| ------------------------- | ---------- |
		| * References              |            |
		|   * Count                 |      4     |
		|     * Branches            |      1     |
		|     * Tags                |      1     |
		|     * Remotes             |      1     |
		|     * Others              |      1     |
		|                           |            |
		| * Reachable objects       |            |
		|   * Count                 |   3.02 k   |
		|     * Commits             |   1.01 k   |
		|     * Trees               |   1.01 k   |
		|     * Blobs               |   1.01 k   |
		|     * Tags                |      1     |
		|   * Inflated size         |  16.03 MiB |
		|     * Commits             | 217.92 KiB |
		|     * Trees               |  15.81 MiB |
		|     * Blobs               |  11.68 KiB |
		|     * Tags                |    132 B   |
		|   * Disk size             | $(object_type_disk_usage all true) |
		|     * Commits             | $(object_type_disk_usage commit true) |
		|     * Trees               | $(object_type_disk_usage tree true) |
		|     * Blobs               |  $(object_type_disk_usage blob true) |
		|     * Tags                |    $(object_type_disk_usage tag) B   |
		|                           |            |
		| * Largest objects         |            |
		|   * Commits               |            |
		|     * Maximum size    [1] |    223 B   |
		|     * Maximum parents [2] |      1     |
		|   * Trees                 |            |
		|     * Maximum size    [3] |  32.29 KiB |
		|     * Maximum entries [4] |   1.01 k   |
		|   * Blobs                 |            |
		|     * Maximum size    [5] |     13 B   |
		|   * Tags                  |            |
		|     * Maximum size    [6] |    132 B   |

		[1] 0dc91eb18580102a3a216c8bfecedeba2b9f9b9a
		[2] 0dc91eb18580102a3a216c8bfecedeba2b9f9b9a
		[3] 60665251ab71dbd8c18d9bf2174f4ee0d58aa06c
		[4] 60665251ab71dbd8c18d9bf2174f4ee0d58aa06c
		[5] 97d808e45116bf02103490294d3d46dad7a2ac62
		[6] 4dae4f5954f5e6feb3577cfb1b181daa3fd3afd2
		EOF

		git repo structure >out 2>err &&

		test_cmp expect out &&
		test_line_count = 0 err
	)
'

test_expect_success SHA1 'lines and nul format' '
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
		objects.commits.max_size=221
		objects.commits.max_size_oid=de3508174b5c2ace6993da67cae9be9069e2df39
		objects.trees.max_size=1335
		objects.trees.max_size_oid=09931deea9d81ec21300d3e13c74412f32eacec5
		objects.blobs.max_size=11
		objects.blobs.max_size_oid=eaeeedced46482bd4281fda5a5f05ce24854151f
		objects.tags.max_size=132
		objects.tags.max_size_oid=1ee0f2b16ea37d895dbe9dbd76cd2ac70446176c
		objects.commits.max_parents=1
		objects.commits.max_parents_oid=de3508174b5c2ace6993da67cae9be9069e2df39
		objects.trees.max_entries=42
		objects.trees.max_entries_oid=09931deea9d81ec21300d3e13c74412f32eacec5
		EOF

		git repo structure --format=lines >out 2>err &&

		test_cmp expect out &&
		test_line_count = 0 err &&

		git repo structure --format=nul >out 2>err &&
		tr "\012\000" "=\012" <out >actual &&

		test_cmp expect actual &&
		test_line_count = 0 err &&

		# "-z", as a synonym to "--format=nul", participates in the
		# usual "last one wins" rule.
		git repo structure --format=table -z >out 2>err &&
		tr "\012\000" "=\012" <out >actual &&

		test_cmp expect actual &&
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
