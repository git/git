#!/bin/sh

test_description='commit graph'
. ./test-lib.sh

test_expect_success 'setup full repo' '
	mkdir full &&
	cd "$TRASH_DIRECTORY/full" &&
	git init &&
	git config core.commitGraph true &&
	objdir=".git/objects" &&
	test_oid_init
'

test_expect_success 'verify graph with no graph file' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph verify
'

test_expect_success 'write graph with no packs' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write --object-dir . &&
	test_path_is_file info/commit-graph
'

test_expect_success 'create commits and repack' '
	cd "$TRASH_DIRECTORY/full" &&
	for i in $(test_seq 3)
	do
		test_commit $i &&
		git branch commits/$i
	done &&
	git repack
'

graph_git_two_modes() {
	git -c core.commitGraph=true $1 >output
	git -c core.commitGraph=false $1 >expect
	test_cmp expect output
}

graph_git_behavior() {
	MSG=$1
	DIR=$2
	BRANCH=$3
	COMPARE=$4
	test_expect_success "check normal git operations: $MSG" '
		cd "$TRASH_DIRECTORY/$DIR" &&
		graph_git_two_modes "log --oneline $BRANCH" &&
		graph_git_two_modes "log --topo-order $BRANCH" &&
		graph_git_two_modes "log --graph $COMPARE..$BRANCH" &&
		graph_git_two_modes "branch -vv" &&
		graph_git_two_modes "merge-base -a $BRANCH $COMPARE"
	'
}

graph_git_behavior 'no graph' full commits/3 commits/1

graph_read_expect() {
	OPTIONAL=""
	NUM_CHUNKS=3
	if test ! -z $2
	then
		OPTIONAL=" $2"
		NUM_CHUNKS=$((3 + $(echo "$2" | wc -w)))
	fi
	cat >expect <<- EOF
	header: 43475048 1 1 $NUM_CHUNKS 0
	num_commits: $1
	chunks: oid_fanout oid_lookup commit_metadata$OPTIONAL
	EOF
	git commit-graph read >output &&
	test_cmp expect output
}

test_expect_success 'write graph' '
	cd "$TRASH_DIRECTORY/full" &&
	graph1=$(git commit-graph write) &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "3"
'

graph_git_behavior 'graph exists' full commits/3 commits/1

test_expect_success 'Add more commits' '
	cd "$TRASH_DIRECTORY/full" &&
	git reset --hard commits/1 &&
	for i in $(test_seq 4 5)
	do
		test_commit $i &&
		git branch commits/$i
	done &&
	git reset --hard commits/2 &&
	for i in $(test_seq 6 7)
	do
		test_commit $i &&
		git branch commits/$i
	done &&
	git reset --hard commits/2 &&
	git merge commits/4 &&
	git branch merge/1 &&
	git reset --hard commits/4 &&
	git merge commits/6 &&
	git branch merge/2 &&
	git reset --hard commits/3 &&
	git merge commits/5 commits/7 &&
	git branch merge/3 &&
	git repack
'

# Current graph structure:
#
#   __M3___
#  /   |   \
# 3 M1 5 M2 7
# |/  \|/  \|
# 2    4    6
# |___/____/
# 1

test_expect_success 'write graph with merges' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "10" "extra_edges"
'

graph_git_behavior 'merge 1 vs 2' full merge/1 merge/2
graph_git_behavior 'merge 1 vs 3' full merge/1 merge/3
graph_git_behavior 'merge 2 vs 3' full merge/2 merge/3

test_expect_success 'Add one more commit' '
	cd "$TRASH_DIRECTORY/full" &&
	test_commit 8 &&
	git branch commits/8 &&
	ls $objdir/pack | grep idx >existing-idx &&
	git repack &&
	ls $objdir/pack| grep idx | grep -v -f existing-idx >new-idx
'

# Current graph structure:
#
#      8
#      |
#   __M3___
#  /   |   \
# 3 M1 5 M2 7
# |/  \|/  \|
# 2    4    6
# |___/____/
# 1

graph_git_behavior 'mixed mode, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'mixed mode, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'write graph with new commit' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "11" "extra_edges"
'

graph_git_behavior 'full graph, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'full graph, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'write graph with nothing new' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "11" "extra_edges"
'

graph_git_behavior 'cleared graph, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'cleared graph, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'build graph from latest pack with closure' '
	cd "$TRASH_DIRECTORY/full" &&
	cat new-idx | git commit-graph write --stdin-packs &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "9" "extra_edges"
'

graph_git_behavior 'graph from pack, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'graph from pack, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'build graph from commits with closure' '
	cd "$TRASH_DIRECTORY/full" &&
	git tag -a -m "merge" tag/merge merge/2 &&
	git rev-parse tag/merge >commits-in &&
	git rev-parse merge/1 >>commits-in &&
	cat commits-in | git commit-graph write --stdin-commits &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "6"
'

graph_git_behavior 'graph from commits, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'graph from commits, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'build graph from commits with append' '
	cd "$TRASH_DIRECTORY/full" &&
	git rev-parse merge/3 | git commit-graph write --stdin-commits --append &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "10" "extra_edges"
'

graph_git_behavior 'append graph, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'append graph, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'build graph using --reachable' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write --reachable &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "11" "extra_edges"
'

graph_git_behavior 'append graph, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'append graph, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'setup bare repo' '
	cd "$TRASH_DIRECTORY" &&
	git clone --bare --no-local full bare &&
	cd bare &&
	git config core.commitGraph true &&
	baredir="./objects"
'

graph_git_behavior 'bare repo, commit 8 vs merge 1' bare commits/8 merge/1
graph_git_behavior 'bare repo, commit 8 vs merge 2' bare commits/8 merge/2

test_expect_success 'write graph in bare repo' '
	cd "$TRASH_DIRECTORY/bare" &&
	git commit-graph write &&
	test_path_is_file $baredir/info/commit-graph &&
	graph_read_expect "11" "extra_edges"
'

graph_git_behavior 'bare repo with graph, commit 8 vs merge 1' bare commits/8 merge/1
graph_git_behavior 'bare repo with graph, commit 8 vs merge 2' bare commits/8 merge/2

test_expect_success 'perform fast-forward merge in full repo' '
	cd "$TRASH_DIRECTORY/full" &&
	git checkout -b merge-5-to-8 commits/5 &&
	git merge commits/8 &&
	git show-ref -s merge-5-to-8 >output &&
	git show-ref -s commits/8 >expect &&
	test_cmp expect output
'

test_expect_success 'check that gc computes commit-graph' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit --allow-empty -m "blank" &&
	git commit-graph write --reachable &&
	cp $objdir/info/commit-graph commit-graph-before-gc &&
	git reset --hard HEAD~1 &&
	git config gc.writeCommitGraph true &&
	git gc &&
	cp $objdir/info/commit-graph commit-graph-after-gc &&
	! test_cmp_bin commit-graph-before-gc commit-graph-after-gc &&
	git commit-graph write --reachable &&
	test_cmp_bin commit-graph-after-gc $objdir/info/commit-graph
'

test_expect_success 'replace-objects invalidates commit-graph' '
	cd "$TRASH_DIRECTORY" &&
	test_when_finished rm -rf replace &&
	git clone full replace &&
	(
		cd replace &&
		git commit-graph write --reachable &&
		test_path_is_file .git/objects/info/commit-graph &&
		git replace HEAD~1 HEAD~2 &&
		git -c core.commitGraph=false log >expect &&
		git -c core.commitGraph=true log >actual &&
		test_cmp expect actual &&
		git commit-graph write --reachable &&
		git -c core.commitGraph=false --no-replace-objects log >expect &&
		git -c core.commitGraph=true --no-replace-objects log >actual &&
		test_cmp expect actual &&
		rm -rf .git/objects/info/commit-graph &&
		git commit-graph write --reachable &&
		test_path_is_file .git/objects/info/commit-graph
	)
'

test_expect_success 'commit grafts invalidate commit-graph' '
	cd "$TRASH_DIRECTORY" &&
	test_when_finished rm -rf graft &&
	git clone full graft &&
	(
		cd graft &&
		git commit-graph write --reachable &&
		test_path_is_file .git/objects/info/commit-graph &&
		H1=$(git rev-parse --verify HEAD~1) &&
		H3=$(git rev-parse --verify HEAD~3) &&
		echo "$H1 $H3" >.git/info/grafts &&
		git -c core.commitGraph=false log >expect &&
		git -c core.commitGraph=true log >actual &&
		test_cmp expect actual &&
		git commit-graph write --reachable &&
		git -c core.commitGraph=false --no-replace-objects log >expect &&
		git -c core.commitGraph=true --no-replace-objects log >actual &&
		test_cmp expect actual &&
		rm -rf .git/objects/info/commit-graph &&
		git commit-graph write --reachable &&
		test_path_is_missing .git/objects/info/commit-graph
	)
'

test_expect_success 'replace-objects invalidates commit-graph' '
	cd "$TRASH_DIRECTORY" &&
	test_when_finished rm -rf shallow &&
	git clone --depth 2 "file://$TRASH_DIRECTORY/full" shallow &&
	(
		cd shallow &&
		git commit-graph write --reachable &&
		test_path_is_missing .git/objects/info/commit-graph &&
		git fetch origin --unshallow &&
		git commit-graph write --reachable &&
		test_path_is_file .git/objects/info/commit-graph
	)
'

# the verify tests below expect the commit-graph to contain
# exactly the commits reachable from the commits/8 branch.
# If the file changes the set of commits in the list, then the
# offsets into the binary file will result in different edits
# and the tests will likely break.

test_expect_success 'git commit-graph verify' '
	cd "$TRASH_DIRECTORY/full" &&
	git rev-parse commits/8 | git commit-graph write --stdin-commits &&
	git commit-graph verify >output
'

NUM_COMMITS=9
NUM_OCTOPUS_EDGES=2
HASH_LEN="$(test_oid rawsz)"
GRAPH_BYTE_VERSION=4
GRAPH_BYTE_HASH=5
GRAPH_BYTE_CHUNK_COUNT=6
GRAPH_CHUNK_LOOKUP_OFFSET=8
GRAPH_CHUNK_LOOKUP_WIDTH=12
GRAPH_CHUNK_LOOKUP_ROWS=5
GRAPH_BYTE_OID_FANOUT_ID=$GRAPH_CHUNK_LOOKUP_OFFSET
GRAPH_BYTE_OID_LOOKUP_ID=$(($GRAPH_CHUNK_LOOKUP_OFFSET + \
			    1 * $GRAPH_CHUNK_LOOKUP_WIDTH))
GRAPH_BYTE_COMMIT_DATA_ID=$(($GRAPH_CHUNK_LOOKUP_OFFSET + \
			     2 * $GRAPH_CHUNK_LOOKUP_WIDTH))
GRAPH_FANOUT_OFFSET=$(($GRAPH_CHUNK_LOOKUP_OFFSET + \
		       $GRAPH_CHUNK_LOOKUP_WIDTH * $GRAPH_CHUNK_LOOKUP_ROWS))
GRAPH_BYTE_FANOUT1=$(($GRAPH_FANOUT_OFFSET + 4 * 4))
GRAPH_BYTE_FANOUT2=$(($GRAPH_FANOUT_OFFSET + 4 * 255))
GRAPH_OID_LOOKUP_OFFSET=$(($GRAPH_FANOUT_OFFSET + 4 * 256))
GRAPH_BYTE_OID_LOOKUP_ORDER=$(($GRAPH_OID_LOOKUP_OFFSET + $HASH_LEN * 8))
GRAPH_BYTE_OID_LOOKUP_MISSING=$(($GRAPH_OID_LOOKUP_OFFSET + $HASH_LEN * 4 + 10))
GRAPH_COMMIT_DATA_OFFSET=$(($GRAPH_OID_LOOKUP_OFFSET + $HASH_LEN * $NUM_COMMITS))
GRAPH_BYTE_COMMIT_TREE=$GRAPH_COMMIT_DATA_OFFSET
GRAPH_BYTE_COMMIT_PARENT=$(($GRAPH_COMMIT_DATA_OFFSET + $HASH_LEN))
GRAPH_BYTE_COMMIT_EXTRA_PARENT=$(($GRAPH_COMMIT_DATA_OFFSET + $HASH_LEN + 4))
GRAPH_BYTE_COMMIT_WRONG_PARENT=$(($GRAPH_COMMIT_DATA_OFFSET + $HASH_LEN + 3))
GRAPH_BYTE_COMMIT_GENERATION=$(($GRAPH_COMMIT_DATA_OFFSET + $HASH_LEN + 11))
GRAPH_BYTE_COMMIT_DATE=$(($GRAPH_COMMIT_DATA_OFFSET + $HASH_LEN + 12))
GRAPH_COMMIT_DATA_WIDTH=$(($HASH_LEN + 16))
GRAPH_OCTOPUS_DATA_OFFSET=$(($GRAPH_COMMIT_DATA_OFFSET + \
			     $GRAPH_COMMIT_DATA_WIDTH * $NUM_COMMITS))
GRAPH_BYTE_OCTOPUS=$(($GRAPH_OCTOPUS_DATA_OFFSET + 4))
GRAPH_BYTE_FOOTER=$(($GRAPH_OCTOPUS_DATA_OFFSET + 4 * $NUM_OCTOPUS_EDGES))

# usage: corrupt_graph_and_verify <position> <data> <string> [<zero_pos>]
# Manipulates the commit-graph file at the position
# by inserting the data, optionally zeroing the file
# starting at <zero_pos>, then runs 'git commit-graph verify'
# and places the output in the file 'err'. Test 'err' for
# the given string.
corrupt_graph_and_verify() {
	pos=$1
	data="${2:-\0}"
	grepstr=$3
	cd "$TRASH_DIRECTORY/full" &&
	orig_size=$(wc -c < $objdir/info/commit-graph) &&
	zero_pos=${4:-${orig_size}} &&
	test_when_finished mv commit-graph-backup $objdir/info/commit-graph &&
	cp $objdir/info/commit-graph commit-graph-backup &&
	printf "$data" | dd of="$objdir/info/commit-graph" bs=1 seek="$pos" conv=notrunc &&
	dd of="$objdir/info/commit-graph" bs=1 seek="$zero_pos" count=0 &&
	generate_zero_bytes $(($orig_size - $zero_pos)) >>"$objdir/info/commit-graph" &&
	test_must_fail git commit-graph verify 2>test_err &&
	grep -v "^+" test_err >err &&
	test_i18ngrep "$grepstr" err
}

test_expect_success 'detect bad signature' '
	corrupt_graph_and_verify 0 "\0" \
		"graph signature"
'

test_expect_success 'detect bad version' '
	corrupt_graph_and_verify $GRAPH_BYTE_VERSION "\02" \
		"graph version"
'

test_expect_success 'detect bad hash version' '
	corrupt_graph_and_verify $GRAPH_BYTE_HASH "\02" \
		"hash version"
'

test_expect_success 'detect low chunk count' '
	corrupt_graph_and_verify $GRAPH_BYTE_CHUNK_COUNT "\02" \
		"missing the .* chunk"
'

test_expect_success 'detect missing OID fanout chunk' '
	corrupt_graph_and_verify $GRAPH_BYTE_OID_FANOUT_ID "\0" \
		"missing the OID Fanout chunk"
'

test_expect_success 'detect missing OID lookup chunk' '
	corrupt_graph_and_verify $GRAPH_BYTE_OID_LOOKUP_ID "\0" \
		"missing the OID Lookup chunk"
'

test_expect_success 'detect missing commit data chunk' '
	corrupt_graph_and_verify $GRAPH_BYTE_COMMIT_DATA_ID "\0" \
		"missing the Commit Data chunk"
'

test_expect_success 'detect incorrect fanout' '
	corrupt_graph_and_verify $GRAPH_BYTE_FANOUT1 "\01" \
		"fanout value"
'

test_expect_success 'detect incorrect fanout final value' '
	corrupt_graph_and_verify $GRAPH_BYTE_FANOUT2 "\01" \
		"fanout value"
'

test_expect_success 'detect incorrect OID order' '
	corrupt_graph_and_verify $GRAPH_BYTE_OID_LOOKUP_ORDER "\01" \
		"incorrect OID order"
'

test_expect_success 'detect OID not in object database' '
	corrupt_graph_and_verify $GRAPH_BYTE_OID_LOOKUP_MISSING "\01" \
		"from object database"
'

test_expect_success 'detect incorrect tree OID' '
	corrupt_graph_and_verify $GRAPH_BYTE_COMMIT_TREE "\01" \
		"root tree OID for commit"
'

test_expect_success 'detect incorrect parent int-id' '
	corrupt_graph_and_verify $GRAPH_BYTE_COMMIT_PARENT "\01" \
		"invalid parent"
'

test_expect_success 'detect extra parent int-id' '
	corrupt_graph_and_verify $GRAPH_BYTE_COMMIT_EXTRA_PARENT "\00" \
		"is too long"
'

test_expect_success 'detect wrong parent' '
	corrupt_graph_and_verify $GRAPH_BYTE_COMMIT_WRONG_PARENT "\01" \
		"commit-graph parent for"
'

test_expect_success 'detect incorrect generation number' '
	corrupt_graph_and_verify $GRAPH_BYTE_COMMIT_GENERATION "\070" \
		"generation for commit"
'

test_expect_success 'detect incorrect generation number' '
	corrupt_graph_and_verify $GRAPH_BYTE_COMMIT_GENERATION "\01" \
		"non-zero generation number"
'

test_expect_success 'detect incorrect commit date' '
	corrupt_graph_and_verify $GRAPH_BYTE_COMMIT_DATE "\01" \
		"commit date"
'

test_expect_success 'detect incorrect parent for octopus merge' '
	corrupt_graph_and_verify $GRAPH_BYTE_OCTOPUS "\01" \
		"invalid parent"
'

test_expect_success 'detect invalid checksum hash' '
	corrupt_graph_and_verify $GRAPH_BYTE_FOOTER "\00" \
		"incorrect checksum"
'

test_expect_success 'detect incorrect chunk count' '
	corrupt_graph_and_verify $GRAPH_BYTE_CHUNK_COUNT "\377" \
		"chunk lookup table entry missing" $GRAPH_CHUNK_LOOKUP_OFFSET
'

test_expect_success 'git fsck (checks commit-graph)' '
	cd "$TRASH_DIRECTORY/full" &&
	git fsck &&
	corrupt_graph_and_verify $GRAPH_BYTE_FOOTER "\00" \
		"incorrect checksum" &&
	test_must_fail git fsck
'

test_expect_success 'setup non-the_repository tests' '
	rm -rf repo &&
	git init repo &&
	test_commit -C repo one &&
	test_commit -C repo two &&
	git -C repo config core.commitGraph true &&
	git -C repo rev-parse two | \
		git -C repo commit-graph write --stdin-commits
'

test_expect_success 'parse_commit_in_graph works for non-the_repository' '
	test-tool repository parse_commit_in_graph \
		repo/.git repo "$(git -C repo rev-parse two)" >actual &&
	{
		git -C repo log --pretty=format:"%ct " -1 &&
		git -C repo rev-parse one
	} >expect &&
	test_cmp expect actual &&

	test-tool repository parse_commit_in_graph \
		repo/.git repo "$(git -C repo rev-parse one)" >actual &&
	git -C repo log --pretty="%ct" -1 one >expect &&
	test_cmp expect actual
'

test_expect_success 'get_commit_tree_in_graph works for non-the_repository' '
	test-tool repository get_commit_tree_in_graph \
		repo/.git repo "$(git -C repo rev-parse two)" >actual &&
	git -C repo rev-parse two^{tree} >expect &&
	test_cmp expect actual &&

	test-tool repository get_commit_tree_in_graph \
		repo/.git repo "$(git -C repo rev-parse one)" >actual &&
	git -C repo rev-parse one^{tree} >expect &&
	test_cmp expect actual
'

test_done
