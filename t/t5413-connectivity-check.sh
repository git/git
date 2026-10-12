#!/bin/sh

test_description='connectivity check (transfer.connectivityCheck)'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_oid_cache <<-\EOF
missing sha1:0000000000000000000000000000000000000001
missing sha256:0000000000000000000000000000000000000000000000000000000000000001
EOF

set_connectivity_check () {
	git -C "$1" config transfer.connectivityCheck "$2"
}

# Run a connectivity check via rev-list directly.  Uses $mode
# (set by the enclosing for-loop) to choose full or incremental.
check_connected () {
	flags= &&
	if test "$mode" = incremental
	then
		flags=--verify-trees-incremental
	fi &&
	printf '%s\n' "$@" |
	git rev-list $flags \
		--objects --stdin --not --all --quiet \
		--exclude-promisor-objects
}

# Run git with a temporary index, leaving the real index untouched.
tmpgit () {
	GIT_INDEX_FILE=.git/tmp-idx git "$@"
}

# Create a commit with one file changed, without modifying HEAD,
# index, or worktree.  Prints the new commit OID on stdout.
# Usage: commit_with_change <parent> <path> <content>
commit_with_change () {
	new_blob=$(echo "$3" | git hash-object -w --stdin) &&
	tmpgit read-tree "$1" &&
	tmpgit update-index --replace \
		--cacheinfo "100644,$new_blob,$2" &&
	new_tree=$(tmpgit write-tree) &&
	rm -f .git/tmp-idx &&
	git commit-tree "$new_tree" -p "$1" -m "modify $2"
}

# Check OIDs and optionally verify trace2 counts.
# Usage: check_connected_trace <trace-file> <trees> <blobs> <oid>...
# An empty string for <trees> or <blobs> skips that assertion.
check_connected_trace () {
	trace_file=$1 trees=$2 blobs=$3 &&
	shift 3 &&
	test_env GIT_TRACE2_EVENT="$(pwd)/$trace_file" \
		check_connected "$@" &&
	if test "$mode" != incremental
	then
		return
	fi &&
	if test -n "$trees"
	then
		test_trace2_data_singular connectivity trees_loaded "$trees" \
			<"$trace_file"
	fi &&
	if test -n "$blobs"
	then
		test_trace2_data_singular connectivity blobs_checked "$blobs" \
			<"$trace_file"
	fi
}

# Shared setup: a repo with several root-level files and nested dirs.
# The unchanged/ subtree (10 dirs x 10 files = 100 blobs, 11 trees)
# acts as a canary: any test asserting small tree/blob counts would
# fail dramatically if incremental accidentally walked into it.

test_expect_success 'setup main repo' '
	git init main-repo &&
	(
		cd main-repo &&
		for i in $(test_seq 1 5)
		do
			echo "file $i" >"file-$i.txt" || return 1
		done &&
		git add file-*.txt &&
		git commit -m "initial" &&

		mkdir -p a/b/c &&
		echo deep >a/b/c/deep.txt &&
		echo other >a/other.txt &&
		git add a/b/c/deep.txt a/other.txt &&
		git commit -m "add nested dirs" &&

		for i in $(test_seq 1 10)
		do
			d="unchanged/dir-$i" &&
			mkdir -p "$d" &&
			for j in $(test_seq 1 10)
			do
				echo "$i $j" >"$d/file-$j.txt" || return 1
			done
		done &&
		git add unchanged/ &&
		git commit -m "add unchanged canary subtree"
	)
'

test_expect_success 'setup replacement object repo' '
	git init replace-test &&
	(
		cd replace-test &&

		test_commit --no-tag original file.txt &&
		original=$(git rev-parse HEAD) &&
		orig_blob=$(git rev-parse HEAD:file.txt) &&

		# Orphan replacement commit with a different tree
		replacement_tree=$(echo replaced | git hash-object -w --stdin |
			xargs -I{} git mktree <<-EOF
			100644 blob {}	file.txt
			EOF
		) &&
		replacement=$(git commit-tree -m "replacement" \
			"$replacement_tree") &&

		git replace "$original" "$replacement" &&

		# Remove the original blob so only the replacement
		# tree is complete.
		rm .git/objects/$(test_oid_to_path "$orig_blob") &&

		# Drop branch and HEAD so --not --all does not
		# exclude the original commit.
		git update-ref -d refs/heads/main &&
		git update-ref -d HEAD &&

		echo "$original" >.git/test-oid
	)
'

missing_oid=$(test_oid missing)
original_oid=$(cat replace-test/.git/test-oid)

for mode in full incremental
do

# Corruption detection: craft broken object graphs and verify detection.
# All tests use main-repo without modifying its refs or worktree.

test_expect_success "$mode: rejects commit with missing blob" '
	(
		cd main-repo &&
		bad_tree=$(printf "100644 blob ${missing_oid}\tfile.txt\n" |
			git mktree --missing) &&
		bad_commit=$(git commit-tree "$bad_tree" -p HEAD -m "bad") &&
		test_expect_code 128 check_connected "$bad_commit" 2>err &&
		test_grep "missing blob object" err
	)
'

test_expect_success "$mode: rejects commit with missing subtree" '
	(
		cd main-repo &&
		bad_tree=$(printf "40000 tree ${missing_oid}\tdir\n" |
			git mktree --missing) &&
		bad_commit=$(git commit-tree "$bad_tree" -p HEAD -m "bad") &&
		test_expect_code 128 check_connected "$bad_commit" 2>err &&
		test_grep "bad tree object" err
	)
'

test_expect_success "$mode: verifies direct tree tip" '
	(
		cd main-repo &&
		bad_tree=$(printf "100644 blob ${missing_oid}\tfile.txt\n" |
			git mktree --missing) &&
		test_expect_code 128 check_connected "$bad_tree" 2>err &&
		test_grep "missing blob object" err
	)
'

test_expect_success "$mode: verifies direct blob tip" '
	(
		cd main-repo &&
		blob_oid=$(echo "hello" | git hash-object -w --stdin) &&
		check_connected "$blob_oid"
	)
'

test_expect_success "$mode: rejects missing direct blob tip" '
	(
		cd main-repo &&
		test_expect_code 128 check_connected \
			"$missing_oid" 2>err
	)
'

test_expect_success PERL_TEST_HELPERS \
	"$mode: rejects blob OID reused as tree entry" '
	(
		cd main-repo &&

		blob_oid=$(git rev-parse HEAD:file-1.txt) &&
		bin_oid=$(echo "$blob_oid" | hex2oct) &&

		bad_tree=$(printf "40000 subdir\0$bin_oid" |
			git hash-object -t tree -w --stdin) &&
		bad_commit=$(git commit-tree -p HEAD -m "child" "$bad_tree") &&

		test_expect_code 128 check_connected "$bad_commit" 2>err &&
		test_grep "not a tree" err
	)
'

test_expect_success PERL_TEST_HELPERS \
	"$mode: rejects tree OID reused as blob entry" '
	(
		cd main-repo &&

		tree_oid=$(git rev-parse HEAD:a) &&
		bin_oid=$(echo "$tree_oid" | hex2oct) &&

		bad_tree=$(printf "100644 fakefile\0$bin_oid" |
			git hash-object -t tree -w --stdin) &&
		bad_commit=$(git commit-tree -p HEAD -m "child" "$bad_tree") &&

		test_expect_code 128 check_connected "$bad_commit" 2>err &&
		test_grep "not a blob" err
	)
'

test_expect_success "$mode: checks multiple tips" '
	(
		cd main-repo &&
		c1=$(commit_with_change HEAD file-1.txt "tip-a") &&
		c2=$(commit_with_change HEAD file-2.txt "tip-b") &&
		git tag -a -m "tagged" multi-tag "$c1" &&
		tag_oid=$(git rev-parse multi-tag) &&
		git tag -d multi-tag &&
		check_connected "$c2" "$tag_oid"
	)
'

# Tree-diff optimization: verify trace2 counts.

test_expect_success "$mode: handles root commit (no parents)" '
	(
		cd main-repo &&
		blob_oid=$(echo "root-content" | git hash-object -w --stdin) &&
		tree_oid=$(printf "100644 blob %s\tfile.txt\n" \
			"$blob_oid" | git mktree) &&
		root_oid=$(git commit-tree "$tree_oid" -m "root") &&

		# No parent trees, so the full tree is verified.
		# 1 tree loaded (root), 1 blob checked.
		check_connected_trace trace-root.txt 1 1 "$root_oid"
	)
'

test_expect_success "$mode: handles single file change" '
	(
		cd main-repo &&
		oid=$(commit_with_change HEAD file-1.txt "changed") &&

		# 2 trees loaded (new root + parent root), 1 blob checked.
		check_connected_trace trace-flat.txt 2 1 "$oid"
	)
'

test_expect_success "$mode: handles nested change" '
	(
		cd main-repo &&
		oid=$(commit_with_change HEAD a/b/c/deep.txt "deep-changed") &&
		# 4 new trees + 4 parent trees = 8 loaded, 1 blob checked.
		check_connected_trace trace-nested.txt 8 1 "$oid"
	)
'

test_expect_success "$mode: handles change-then-revert" '
	(
		cd main-repo &&
		c1=$(commit_with_change HEAD file-1.txt "revert-tmp") &&
		c2=$(commit_with_change "$c1" file-1.txt "file 1") &&
		c3=$(commit_with_change "$c2" file-1.txt "revert-final") &&

		# c1: new root + parent root = 2 loads.  c2: root matches
		# HEAD (already trusted), skipped.  c3: new root + parent
		# already expanded = 1 load.  Total: 3 trees, 2 blobs.
		check_connected_trace trace-revert.txt 3 2 "$c3"
	)
'

test_expect_success "$mode: handles subtree moved to another path" '
	(
		cd main-repo &&
		moved_tree=$(git ls-tree HEAD |
			sed "s/	a$/	moved/" |
			git mktree) &&
		moved=$(git commit-tree "$moved_tree" -p HEAD -m move) &&
		# New root + parent root scanned, but the moved subtree
		# (same OID) is trusted and not descended into.
		check_connected_trace trace-move.txt 2 0 "$moved"
	)
'

test_expect_success "$mode: handles multi-parent merge" '
	(
		cd main-repo &&
		left=$(commit_with_change HEAD file-1.txt left) &&
		right=$(commit_with_change HEAD file-2.txt right) &&
		git update-ref refs/heads/left "$left" &&
		git update-ref refs/heads/right "$right" &&
		left_blob=$(git rev-parse "$left:file-1.txt") &&
		right_blob=$(git rev-parse "$right:file-2.txt") &&
		tmpgit read-tree HEAD &&
		tmpgit update-index --replace \
			--cacheinfo "100644,$left_blob,file-1.txt" &&
		tmpgit update-index --replace \
			--cacheinfo "100644,$right_blob,file-2.txt" &&
		merge_tree=$(tmpgit write-tree) &&
		rm -f .git/tmp-idx &&
		merge=$(git commit-tree "$merge_tree" \
			-p "$left" -p "$right" -m merge) &&
		# Both parent root trees are scanned as bases, so
		# blobs from each parent are trusted without ODB checks.
		check_connected_trace trace-merge.txt 3 0 "$merge" &&
		git update-ref -d refs/heads/left &&
		git update-ref -d refs/heads/right
	)
'

test_expect_success "$mode: handles merge with incoming and boundary parents" '
	(
		cd main-repo &&
		# parent1 is incoming (not a ref), HEAD is boundary.
		parent1=$(commit_with_change HEAD file-1.txt merge-inc) &&
		merge=$(git commit-tree \
			"$(git rev-parse "$parent1^{tree}")" \
			-p "$parent1" -p HEAD -m merge-mixed) &&
		# parent1 verified first (topo order): 2 trees, 1 blob.
		# merge tree = parent1 tree (already trusted): 0 extra.
		check_connected_trace trace-merge-inc.txt 2 1 "$merge"
	)
'

test_expect_success "$mode: handles gitlink entries (submodules)" '
	(
		cd main-repo &&
		tmpgit read-tree HEAD &&
		tmpgit update-index --add \
			--cacheinfo "160000,$missing_oid,my-submodule" &&
		gitlink_tree=$(tmpgit write-tree) &&
		rm -f .git/tmp-idx &&
		gitlink_commit=$(git commit-tree "$gitlink_tree" -p HEAD \
			-m "add gitlink") &&

		# Gitlink entries are skipped -- the missing submodule
		# commit OID does not cause a failure.
		check_connected_trace trace-gitlink.txt 2 0 "$gitlink_commit"
	)
'

test_expect_success "$mode: handles file-to-directory transition" '
	(
		cd main-repo &&

		# Parent: "foo" is a blob at root.
		blob_a=$(echo "file-content" | git hash-object -w --stdin) &&
		tmpgit read-tree HEAD &&
		tmpgit update-index --add \
			--cacheinfo "100644,$blob_a,foo" &&
		parent_tree=$(tmpgit write-tree) &&
		rm -f .git/tmp-idx &&
		parent=$(git commit-tree "$parent_tree" -p HEAD \
			-m "add foo as file") &&

		# Child: "foo" becomes a directory (foo/bar.txt).
		blob_b=$(echo "dir-content" | git hash-object -w --stdin) &&
		tmpgit read-tree "$parent" &&
		tmpgit update-index --remove foo &&
		tmpgit update-index --add \
			--cacheinfo "100644,$blob_b,foo/bar.txt" &&
		child_tree=$(tmpgit write-tree) &&
		rm -f .git/tmp-idx &&
		child=$(git commit-tree "$child_tree" -p "$parent" \
			-m "foo: file to directory") &&
		check_connected_trace trace-f2d.txt "" "" "$child"
	)
'

test_expect_success "$mode: handles directory-to-file transition" '
	(
		cd main-repo &&

		# Parent: "bar/baz.txt" exists (bar is a directory).
		blob_a=$(echo "nested" | git hash-object -w --stdin) &&
		tmpgit read-tree HEAD &&
		tmpgit update-index --add \
			--cacheinfo "100644,$blob_a,bar/baz.txt" &&
		parent_tree=$(tmpgit write-tree) &&
		rm -f .git/tmp-idx &&
		parent=$(git commit-tree "$parent_tree" -p HEAD \
			-m "add bar as directory") &&

		# Child: "bar" becomes a plain file.
		blob_b=$(echo "flat" | git hash-object -w --stdin) &&
		tmpgit read-tree "$parent" &&
		tmpgit update-index --remove bar/baz.txt &&
		tmpgit update-index --add \
			--cacheinfo "100644,$blob_b,bar" &&
		child_tree=$(tmpgit write-tree) &&
		rm -f .git/tmp-idx &&
		child=$(git commit-tree "$child_tree" -p "$parent" \
			-m "bar: directory to file") &&
		check_connected_trace trace-d2f.txt "" "" "$child"
	)
'

# Replacement objects.

test_expect_success "$mode: accepts with replacement objects" '
	(
		cd replace-test &&
		check_connected "$original_oid"
	)
'

test_expect_success "$mode: rejects without replacement objects" '
	(
		cd replace-test &&
		GIT_NO_REPLACE_OBJECTS=1 &&
		export GIT_NO_REPLACE_OBJECTS &&
		test_expect_code 128 check_connected \
			"$original_oid" 2>err &&
		test_grep "missing blob object" err
	)
'

test_expect_success "$mode: accepts missing promised blob" '
	test_when_finished "rm -rf prom-src prom-server.git prom-client" &&
	git init prom-src &&
	test_commit -C prom-src --no-tag base file.txt original &&
	test_commit -C prom-src --no-tag "add file2" file2.txt extra &&
	git clone --bare prom-src prom-server.git &&
	git -C prom-server.git config uploadpack.allowfilter true &&
	git -C prom-server.git config uploadpack.allowanysha1inwant true &&
	git clone --no-checkout --filter=blob:none \
		"file://$(pwd)/prom-server.git" prom-client &&
	(
		cd prom-client &&
		promised_blob=$(git rev-parse HEAD:file2.txt) &&
		test_must_fail env GIT_NO_LAZY_FETCH=1 \
			git cat-file -e "$promised_blob" &&
		new_tree=$(printf "100644 blob %s\tnewname.txt\n" \
			"$promised_blob" |
			git mktree --missing) &&
		new_commit=$(git commit-tree "$new_tree" \
			-p HEAD -m "reuse promised blob") &&
		check_connected "$new_commit" &&
		test_must_fail env GIT_NO_LAZY_FETCH=1 \
			git cat-file -e "$promised_blob"
	)
'

test_expect_success "$mode: does not lazy-fetch promised blob" '
	test_when_finished "rm -rf nofetch-src nofetch-server.git nofetch-client" &&
	git init nofetch-src &&
	test_commit -C nofetch-src --no-tag base file.txt content &&
	git clone --bare nofetch-src nofetch-server.git &&
	git -C nofetch-server.git config uploadpack.allowfilter true &&
	git -C nofetch-server.git config uploadpack.allowanysha1inwant true &&
	git clone --no-checkout --filter=blob:none \
		"file://$(pwd)/nofetch-server.git" nofetch-client &&
	(
		cd nofetch-client &&
		promised_blob=$(git rev-parse HEAD:file.txt) &&
		test_must_fail env GIT_NO_LAZY_FETCH=1 \
			git cat-file -e "$promised_blob" &&
		new_tree=$(printf "100644 blob %s\tfile.txt\n" \
			"$promised_blob" |
			git mktree --missing) &&
		new_commit=$(git commit-tree "$new_tree" \
			-m "root commit with promised blob") &&
		check_connected "$new_commit" &&
		test_must_fail env GIT_NO_LAZY_FETCH=1 \
			git cat-file -e "$promised_blob"
	)
'

test_expect_success "$mode: accepts missing promised tree" '
	test_when_finished "rm -rf prom-tree-src prom-tree-server.git prom-tree-client" &&
	git init prom-tree-src &&
	mkdir -p prom-tree-src/a/b &&
	test_commit -C prom-tree-src --no-tag "nested dirs" a/b/file.txt deep &&
	git clone --bare prom-tree-src prom-tree-server.git &&
	git -C prom-tree-server.git config uploadpack.allowfilter true &&
	git -C prom-tree-server.git config uploadpack.allowanysha1inwant true &&
	git clone --no-checkout --filter=tree:1 \
		"file://$(pwd)/prom-tree-server.git" prom-tree-client &&
	(
		cd prom-tree-client &&
		promised_tree=$(git ls-tree HEAD -- a |
			awk "{print \$3}") &&
		test_must_fail env GIT_NO_LAZY_FETCH=1 \
			git cat-file -e "$promised_tree" &&
		new_tree=$(printf "40000 tree %s\trenamed\n" \
			"$promised_tree" |
			git mktree --missing) &&
		new_commit=$(git commit-tree "$new_tree" \
			-p HEAD -m "reuse promised tree") &&
		check_connected "$new_commit" &&
		test_must_fail env GIT_NO_LAZY_FETCH=1 \
			git cat-file -e "$promised_tree"
	)
'

test_expect_success "$mode: verifies new subtree when parent subtree is promised" '
	test_when_finished "rm -rf prom-base-src prom-base-server.git prom-base-client" &&
	git init prom-base-src &&
	mkdir -p prom-base-src/a &&
	test_commit -C prom-base-src --no-tag "base" a/file.txt deep &&
	git clone --bare prom-base-src prom-base-server.git &&
	git -C prom-base-server.git config uploadpack.allowfilter true &&
	git -C prom-base-server.git config uploadpack.allowanysha1inwant true &&
	git clone --no-checkout --filter=tree:1 \
		"file://$(pwd)/prom-base-server.git" prom-base-client &&
	(
		cd prom-base-client &&
		parent_subtree=$(git ls-tree HEAD -- a |
			awk "{print \$3}") &&
		test_must_fail env GIT_NO_LAZY_FETCH=1 \
			git cat-file -e "$parent_subtree" &&
		new_blob=$(echo "local-content" | git hash-object -w --stdin) &&
		new_subtree=$(printf "100644 blob %s\tfile.txt\n" \
			"$new_blob" | git mktree) &&
		new_root=$(printf "40000 tree %s\ta\n" \
			"$new_subtree" | git mktree) &&
		new_commit=$(git commit-tree "$new_root" \
			-p HEAD -m "replace promised subtree") &&
		check_connected "$new_commit" &&
		test_must_fail env GIT_NO_LAZY_FETCH=1 \
			git cat-file -e "$parent_subtree"
	)
'

test_expect_success "$mode: verifies local commit in partial clone" '
	test_when_finished "rm -rf pc-src pc-server.git pc-client" &&
	git init pc-src &&
	test_commit -C pc-src --no-tag base file.txt &&
	git clone --bare pc-src pc-server.git &&
	git -C pc-server.git config uploadpack.allowfilter true &&
	git -C pc-server.git config uploadpack.allowanysha1inwant true &&
	git clone --filter=blob:none \
		"file://$(pwd)/pc-server.git" pc-client &&
	(
		cd pc-client &&
		local_commit=$(commit_with_change HEAD file.txt local-content) &&
		check_connected "$local_commit"
	)
'

test_expect_success "$mode: respects shallow boundary" '
	test_when_finished "rm -rf shallow-src shallow" &&
	git init shallow-src &&
	test_commit -C shallow-src --no-tag base file content-1 &&
	mkdir shallow-src/sub &&
	test_commit -C shallow-src --no-tag change sub/other content-2 &&
	git clone --depth=1 "file://$(pwd)/shallow-src" shallow &&
	(
		cd shallow &&
		tip=$(git rev-parse HEAD) &&
		git for-each-ref --format="delete %(refname)" |
			git update-ref --no-deref --stdin &&
		check_connected "$tip"
	)
'

test_expect_success "$mode: deepening fetch succeeds" '
	test_when_finished "rm -rf deepen-src deepen-server.git deepen-client" &&
	git init deepen-src &&
	test_commit -C deepen-src --no-tag c1 file.txt &&
	test_commit -C deepen-src --no-tag c2 file.txt &&
	test_commit -C deepen-src --no-tag c3 file.txt &&
	git clone --bare deepen-src deepen-server.git &&
	git clone --depth=1 "file://$(pwd)/deepen-server.git" deepen-client &&
	set_connectivity_check deepen-client $mode &&
	test -f deepen-client/.git/shallow &&
	GIT_TRACE2_EVENT="$(pwd)/deepen-trace.txt" \
		git -C deepen-client fetch --deepen=2 origin main &&
	# Incremental falls back to full for deepening fetches,
	# so the trees_loaded event should not appear.
	test_grep ! trees_loaded deepen-trace.txt
'

test_expect_success "$mode: malformed tree detected" '
	(
		cd main-repo &&
		echo abc >malformed-tree &&
		malformed_tree=$(git hash-object --literally -t tree -w \
			malformed-tree) &&
		malformed_commit=$(git commit-tree "$malformed_tree" \
			-p HEAD -m malformed) &&
		test_expect_code 128 check_connected \
			"$malformed_commit" 2>err
	)
'

test_expect_success PERL_TEST_HELPERS \
	"$mode: mid-tree corruption detected" '
	(
		cd main-repo &&
		# Build a tree with one valid entry followed by garbage.
		blob_oid=$(echo "valid" | git hash-object -w --stdin) &&
		bin_oid=$(echo "$blob_oid" | hex2oct) &&
		printf "100644 good\0${bin_oid}GARBAGE" >corrupt-mid-tree &&
		corrupt_tree=$(git hash-object --literally -t tree -w \
			corrupt-mid-tree) &&
		corrupt_commit=$(git commit-tree "$corrupt_tree" \
			-p HEAD -m "mid-tree corruption") &&
		test_expect_code 128 check_connected \
			"$corrupt_commit" 2>err &&
		test_grep "too-short tree object" err
	)
'

done

# Algorithm selection.

test_expect_success 'invalid transfer.connectivityCheck is rejected' '
	test_when_finished "rm -rf invalid-cfg-src invalid-cfg-dst" &&
	git init invalid-cfg-src &&
	test_commit -C invalid-cfg-src --no-tag base file.txt &&
	git clone invalid-cfg-src invalid-cfg-dst &&
	test_commit -C invalid-cfg-src --no-tag update file.txt updated &&
	git -C invalid-cfg-dst config transfer.connectivityCheck bogus &&
	test_must_fail git -C invalid-cfg-dst fetch origin main 2>err &&
	test_grep "unknown transfer.connectivityCheck" err
'

test_expect_success 'push uses incremental when configured' '
	test_when_finished "rm -rf int-src int-dst.git" &&
	git init int-src &&
	test_commit -C int-src --no-tag base file.txt &&
	git clone --bare int-src int-dst.git &&
	test_commit -C int-src --no-tag update file.txt updated &&
	set_connectivity_check int-dst.git incremental &&
	GIT_TRACE2_EVENT="$(pwd)/push-trace.txt" \
		git -C int-src push ../int-dst.git main &&
	test_trace2_data_singular connectivity trees_loaded 2 \
		<push-trace.txt
'

test_expect_success 'fetch uses incremental when configured' '
	test_when_finished "rm -rf fetch-src fetch-dst" &&
	git init fetch-src &&
	test_commit -C fetch-src --no-tag base file.txt &&
	git clone fetch-src fetch-dst &&
	test_commit -C fetch-src --no-tag update file.txt updated &&
	set_connectivity_check fetch-dst incremental &&
	GIT_TRACE2_EVENT="$(pwd)/fetch-trace.txt" \
		git -C fetch-dst fetch origin main &&
	test_trace2_data_singular connectivity trees_loaded 2 \
		<fetch-trace.txt
'

test_expect_success 'clone respects transfer.connectivityCheck' '
	test_when_finished "rm -rf clone-src clone-dst" &&
	git init clone-src &&
	test_commit -C clone-src --no-tag base file.txt &&
	GIT_TRACE2_EVENT="$(pwd)/clone-trace.txt" \
		git -c transfer.connectivityCheck=incremental \
		clone --no-local clone-src clone-dst &&
	test_trace2_data_singular connectivity trees_loaded 0 \
		<clone-trace.txt
'

test_done
