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

object_type_max_inflated_size() {
	max=0

	for oid in $(git rev-list --all --objects \
		--filter=object:type=$1 --filter-provided-objects | cut -d" " -f1)
	do
		size=$(git cat-file -s "$oid") || return 1
		test "$size" -gt "$max" && max=$size
	done

	echo "$max"
}

tag_max_chain_depth() {
	max=0

	for oid in $(git rev-list --all --objects \
		--filter=object:type=tag --filter-provided-objects | cut -d" " -f1)
	do
		depth=0
		current=$oid

		while :
		do
			target=$(git cat-file -p "$current" | sed -n "s/^object //p" | sed -n 1p) || return 1
			test -n "$target" || break
			depth=$((depth + 1))
			type=$(git cat-file -t "$target") || return 1
			test "$type" = tag || break
			current=$target
		done

		test "$depth" -gt "$max" && max=$depth
	done

	echo "$max"
}

object_max_inflated_size() {
	max=0

	for type in commit tree blob tag
	do
		type_max=$(object_type_max_inflated_size "$type") || return 1
		test "$type_max" -gt "$max" && max=$type_max
	done

	echo "$max"
}

object_type_max_disk_size() {
	max=0

	for oid in $(git rev-list --all --objects \
		--filter=object:type=$1 --filter-provided-objects | cut -d" " -f1)
	do
		size=$(echo "$oid" | git cat-file --batch-check='%(objectsize:disk)') || return 1
		test "$size" -gt "$max" && max=$size
	done

	echo "$max"
}

reference_count_total() {
	git for-each-ref --format='%(refname)' | sed -n '$='
}

object_type_count() {
	git rev-list --all --objects \
		--filter=object:type=$1 --filter-provided-objects | sed -n '$='
}

object_count_total() {
	commits=$(object_type_count commit) || return 1
	trees=$(object_type_count tree) || return 1
	blobs=$(object_type_count blob) || return 1
	tags=$(object_type_count tag) || return 1

	echo $((commits + trees + blobs + tags))
}

object_type_total_inflated_size() {
	total=0

	for oid in $(git rev-list --all --objects \
		--filter=object:type=$1 --filter-provided-objects | cut -d" " -f1)
	do
		size=$(git cat-file -s "$oid") || return 1
		total=$((total + size))
	done

	echo "$total"
}

object_total_inflated_size() {
	commits=$(object_type_total_inflated_size commit) || return 1
	trees=$(object_type_total_inflated_size tree) || return 1
	blobs=$(object_type_total_inflated_size blob) || return 1
	tags=$(object_type_total_inflated_size tag) || return 1

	echo $((commits + trees + blobs + tags))
}

object_max_disk_size() {
	max=0

	for type in commit tree blob tag
	do
		type_max=$(object_type_max_disk_size "$type") || return 1
		test "$type_max" -gt "$max" && max=$type_max
	done

	echo "$max"
}

commit_max_parent_count() {
	git rev-list --all --parents | awk '
		{ n = NF - 1; if (n > max) max = n }
		END { print max + 0 }
	'
}

tree_max_entry_count() {
	max=0

	for oid in $(git rev-list --all --objects \
		--filter=object:type=tree --filter-provided-objects | cut -d" " -f1)
	do
		entries=$(git cat-file -p "$oid" | wc -l) || return 1
		test $entries -gt $max && max=$entries
	done

	echo $max
}

blob_max_path_length() {
	git rev-list --all --objects \
		--filter=object:type=blob --filter-provided-objects | awk '
		NF > 1 {
			len = length($2)
			if (len > max) max = len
		}
		END { print max + 0 }
	'
}

blob_max_path_depth() {
	git rev-list --all --objects \
		--filter=object:type=blob --filter-provided-objects | awk '
		NF > 1 {
			depth = gsub(/\//, "/", $2) + 1
			if (depth > max) max = depth
		}
		END { print max + 0 }
	'
}

test_expect_success 'empty repository' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		git repo structure >out 2>err &&
		test_grep "Repository structure" out &&
		test_grep "\\* References" out &&
		test_grep "\\* Reachable objects" out &&
		test_grep "Largest disk size" out &&
		test_grep "Deepest tag chain" out &&
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

		git repo structure >out 2>err &&
		test_grep "\\* References" out &&
		test_grep "\\* Reachable objects" out &&
		test_grep "Largest commit" out &&
		test_grep "Largest disk size" out &&
		test_grep "Largest parent count" out &&
		test_grep "Deepest tag chain" out &&
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
		references.count=$(reference_count_total)
		references.branches.count=1
		references.tags.count=1
		references.remotes.count=0
		references.others.count=0
		objects.count=$(object_count_total)
		objects.commits.count=42
		objects.trees.count=42
		objects.blobs.count=42
		objects.tags.count=1
		objects.inflated_size=$(object_total_inflated_size)
		objects.commits.inflated_size=9225
		objects.trees.inflated_size=28554
		objects.blobs.inflated_size=453
		objects.tags.inflated_size=132
		objects.max_inflated_size=$(object_max_inflated_size)
		objects.commits.max_inflated_size=$(object_type_max_inflated_size commit)
		objects.trees.max_inflated_size=$(object_type_max_inflated_size tree)
		objects.blobs.max_inflated_size=$(object_type_max_inflated_size blob)
		objects.tags.max_inflated_size=$(object_type_max_inflated_size tag)
		objects.disk_size=$(object_type_disk_usage all)
		objects.max_disk_size=$(object_max_disk_size)
		objects.commits.max_disk_size=$(object_type_max_disk_size commit)
		objects.trees.max_disk_size=$(object_type_max_disk_size tree)
		objects.blobs.max_disk_size=$(object_type_max_disk_size blob)
		objects.tags.max_disk_size=$(object_type_max_disk_size tag)
		objects.commits.max_parent_count=$(commit_max_parent_count)
		objects.trees.max_entry_count=$(tree_max_entry_count)
		objects.blobs.max_path_length=$(blob_max_path_length)
		objects.blobs.max_path_depth=$(blob_max_path_depth)
		objects.tags.max_chain_depth=$(tag_max_chain_depth)
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
