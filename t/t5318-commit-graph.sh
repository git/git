#!/bin/sh

test_description='commit graph'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-chunk.sh

GIT_TEST_COMMIT_GRAPH_CHANGED_PATHS=0

test_expect_success 'usage' '
	test_expect_code 129 git commit-graph write blah 2>err &&
	test_expect_code 129 git commit-graph write verify
'

test_expect_success 'usage shown without sub-command' '
	test_expect_code 129 git commit-graph 2>err &&
	grep usage: err
'

test_expect_success 'usage shown with an error on unknown sub-command' '
	cat >expect <<-\EOF &&
	error: unknown subcommand: `unknown'\''
	EOF
	test_expect_code 129 git commit-graph unknown 2>stderr &&
	grep error stderr >actual &&
	test_cmp expect actual
'

objdir=".git/objects"

test_expect_success 'setup full repo' '
	git init full
'

test_expect_success POSIXPERM 'tweak umask for modebit tests' '
	umask 022
'

test_expect_success 'verify graph with no graph file' '
	git -C full commit-graph verify
'

test_expect_success 'write graph with no packs' '
	git -C full commit-graph write --object-dir $objdir &&
	test_path_is_missing full/$objdir/info/commit-graph
'

test_expect_success 'exit with correct error on bad input to --stdin-packs' '
	echo doesnotexist >in &&
	test_expect_code 1 git -C full commit-graph write --stdin-packs \
		<in 2>stderr &&
	test_grep "error adding pack" stderr
'

test_expect_success 'create commits and repack' '
	for i in $(test_seq 3)
	do
		test_commit -C full $i &&
		git -C full branch commits/$i || return 1
	done &&
	git -C full repack
'

. "$TEST_DIRECTORY"/lib-commit-graph.sh

graph_git_behavior 'no graph' full commits/3 commits/1

test_expect_success 'exit with correct error on bad input to --stdin-commits' '
	# invalid, non-hex OID
	echo HEAD | test_expect_code 1 git -C full commit-graph write \
		--stdin-commits 2>stderr &&
	test_grep "unexpected non-hex object ID: HEAD" stderr &&
	# non-existent OID
	echo $ZERO_OID | test_expect_code 1 git -C full commit-graph write \
		--stdin-commits 2>stderr &&
	test_grep "invalid object" stderr &&
	# valid commit and tree OID
	git -C full rev-parse HEAD HEAD^{tree} >in &&
	git -C full commit-graph write --stdin-commits <in &&
	graph_read_expect -C full 3 generation_data
'

test_expect_success 'write graph' '
	git -C full commit-graph write &&
	test_path_is_file full/$objdir/info/commit-graph &&
	graph_read_expect -C full 3 generation_data
'

test_expect_success POSIXPERM 'write graph has correct permissions' '
	test_path_is_file full/$objdir/info/commit-graph &&
	echo "-r--r--r--" >expect &&
	test_modebits full/$objdir/info/commit-graph >actual &&
	test_cmp expect actual
'

graph_git_behavior 'graph exists' full commits/3 commits/1

test_expect_success 'Add more commits' '
	git -C full reset --hard commits/1 &&
	for i in $(test_seq 4 5)
	do
		test_commit -C full $i &&
		git -C full branch commits/$i || return 1
	done &&
	git -C full reset --hard commits/2 &&
	for i in $(test_seq 6 7)
	do
		test_commit -C full $i &&
		git -C full branch commits/$i || return 1
	done &&
	git -C full reset --hard commits/2 &&
	git -C full merge commits/4 &&
	git -C full branch merge/1 &&
	git -C full reset --hard commits/4 &&
	git -C full merge commits/6 &&
	git -C full branch merge/2 &&
	git -C full reset --hard commits/3 &&
	git -C full merge commits/5 commits/7 &&
	git -C full branch merge/3 &&
	git -C full repack
'

test_expect_success 'commit-graph write progress off for redirected stderr' '
	git -C full commit-graph write 2>err &&
	test_must_be_empty err
'

test_expect_success 'commit-graph write force progress on for stderr' '
	GIT_PROGRESS_DELAY=0 git -C full commit-graph write --progress 2>err &&
	test_file_not_empty err
'

test_expect_success 'commit-graph write with the --no-progress option' '
	git -C full commit-graph write --no-progress 2>err &&
	test_must_be_empty err
'

test_expect_success 'commit-graph write --stdin-commits progress off for redirected stderr' '
	git -C full rev-parse commits/5 >in &&
	git -C full commit-graph write --stdin-commits <in 2>err &&
	test_must_be_empty err
'

test_expect_success 'commit-graph write --stdin-commits force progress on for stderr' '
	git -C full rev-parse commits/5 >in &&
	GIT_PROGRESS_DELAY=0 git -C full commit-graph write --stdin-commits \
		--progress <in 2>err &&
	test_grep "Collecting commits from input" err
'

test_expect_success 'commit-graph write --stdin-commits with the --no-progress option' '
	git -C full rev-parse commits/5 >in &&
	git -C full commit-graph write --stdin-commits --no-progress <in 2>err &&
	test_must_be_empty err
'

test_expect_success 'commit-graph verify progress off for redirected stderr' '
	git -C full commit-graph verify 2>err &&
	test_must_be_empty err
'

test_expect_success 'commit-graph verify force progress on for stderr' '
	GIT_PROGRESS_DELAY=0 git -C full commit-graph verify --progress 2>err &&
	test_file_not_empty err
'

test_expect_success 'commit-graph verify with the --no-progress option' '
	git -C full commit-graph verify --no-progress 2>err &&
	test_must_be_empty err
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
	git -C full commit-graph write &&
	test_path_is_file full/$objdir/info/commit-graph &&
	graph_read_expect -C full 10 "generation_data extra_edges"
'

graph_git_behavior 'merge 1 vs 2' full merge/1 merge/2
graph_git_behavior 'merge 1 vs 3' full merge/1 merge/3
graph_git_behavior 'merge 2 vs 3' full merge/2 merge/3

test_expect_success 'Add one more commit' '
	test_commit -C full 8 &&
	git -C full branch commits/8 &&
	ls full/$objdir/pack | grep idx >existing-idx &&
	git -C full repack &&
	ls full/$objdir/pack| grep idx | grep -v -f existing-idx >new-idx
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
	git -C full commit-graph write &&
	test_path_is_file full/$objdir/info/commit-graph &&
	graph_read_expect -C full 11 "generation_data extra_edges"
'

graph_git_behavior 'full graph, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'full graph, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'write graph with nothing new' '
	git -C full commit-graph write &&
	test_path_is_file full/$objdir/info/commit-graph &&
	graph_read_expect -C full 11 "generation_data extra_edges"
'

graph_git_behavior 'cleared graph, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'cleared graph, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'build graph from latest pack with closure' '
	git -C full commit-graph write --stdin-packs <new-idx &&
	test_path_is_file full/$objdir/info/commit-graph &&
	graph_read_expect -C full 9 "generation_data extra_edges"
'

graph_git_behavior 'graph from pack, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'graph from pack, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'build graph from commits with closure' '
	git -C full tag -a -m "merge" tag/merge merge/2 &&
	git -C full rev-parse tag/merge >commits-in &&
	git -C full rev-parse merge/1 >>commits-in &&
	git -C full commit-graph write --stdin-commits <commits-in &&
	test_path_is_file full/$objdir/info/commit-graph &&
	graph_read_expect -C full 6 "generation_data"
'

graph_git_behavior 'graph from commits, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'graph from commits, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'build graph from commits with append' '
	git -C full rev-parse merge/3 >in &&
	git -C full commit-graph write --stdin-commits --append <in &&
	test_path_is_file full/$objdir/info/commit-graph &&
	graph_read_expect -C full 10 "generation_data extra_edges"
'

graph_git_behavior 'append graph, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'append graph, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'build graph using --reachable' '
	git -C full commit-graph write --reachable &&
	test_path_is_file full/$objdir/info/commit-graph &&
	graph_read_expect -C full 11 "generation_data extra_edges"
'

graph_git_behavior 'append graph, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'append graph, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'setup bare repo' '
	git clone --bare --no-local full bare
'

graph_git_behavior 'bare repo, commit 8 vs merge 1' bare commits/8 merge/1
graph_git_behavior 'bare repo, commit 8 vs merge 2' bare commits/8 merge/2

test_expect_success 'write graph in bare repo' '
	git -C bare commit-graph write &&
	test_path_is_file bare/objects/info/commit-graph &&
	graph_read_expect -C bare 11 "generation_data extra_edges"
'

graph_git_behavior 'bare repo with graph, commit 8 vs merge 1' bare commits/8 merge/1
graph_git_behavior 'bare repo with graph, commit 8 vs merge 2' bare commits/8 merge/2

test_expect_success 'perform fast-forward merge in full repo' '
	git -C full checkout -b merge-5-to-8 commits/5 &&
	git -C full merge commits/8 &&
	git -C full show-ref -s merge-5-to-8 >output &&
	git -C full show-ref -s commits/8 >expect &&
	test_cmp expect output
'

test_expect_success 'check that gc computes commit-graph' '
	test_commit -C full --no-tag blank &&
	git -C full commit-graph write --reachable &&
	cp full/$objdir/info/commit-graph commit-graph-before-gc &&
	git -C full reset --hard HEAD~1 &&
	test_config -C full gc.writeCommitGraph true &&
	git -C full gc &&
	cp full/$objdir/info/commit-graph commit-graph-after-gc &&
	! test_cmp_bin commit-graph-before-gc commit-graph-after-gc &&
	git -C full commit-graph write --reachable &&
	test_cmp_bin commit-graph-after-gc full/$objdir/info/commit-graph
'

test_expect_success 'replace-objects invalidates commit-graph' '
	test_when_finished rm -rf replace &&
	git clone full replace &&
	(
		cd replace &&
		git commit-graph write --reachable &&
		test_path_is_file .git/objects/info/commit-graph &&
		git replace HEAD~1 HEAD~2 &&
		graph_git_two_modes "commit-graph verify" &&
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
	test_when_finished rm -rf graft &&
	git clone --template= full graft &&
	(
		cd graft &&
		git commit-graph write --reachable &&
		test_path_is_file .git/objects/info/commit-graph &&
		H1=$(git rev-parse --verify HEAD~1) &&
		H3=$(git rev-parse --verify HEAD~3) &&
		mkdir .git/info &&
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

test_expect_success 'warn on improper hash version' '
	git init --object-format=sha1 sha1 &&
	(
		cd sha1 &&
		test_commit 1 &&
		git commit-graph write --reachable &&
		mv .git/objects/info/commit-graph ../cg-sha1
	) &&
	git init --object-format=sha256 sha256 &&
	(
		cd sha256 &&
		test_commit 1 &&
		git commit-graph write --reachable &&
		mv .git/objects/info/commit-graph ../cg-sha256
	) &&
	(
		cd sha1 &&
		mv ../cg-sha256 .git/objects/info/commit-graph &&
		git log -1 2>err &&
		test_grep "commit-graph hash version 2 does not match version 1" err
	) &&
	(
		cd sha256 &&
		mv ../cg-sha1 .git/objects/info/commit-graph &&
		git log -1 2>err &&
		test_grep "commit-graph hash version 1 does not match version 2" err
	)
'

test_expect_success TIME_IS_64BIT,TIME_T_IS_64BIT 'lower layers have overflow chunk' '
	UNIX_EPOCH_ZERO="@0 +0000" &&
	FUTURE_DATE="@4147483646 +0000" &&
	rm -f full/.git/objects/info/commit-graph &&
	test_commit -C full --date "$FUTURE_DATE" future-1 &&
	test_commit -C full --date "$UNIX_EPOCH_ZERO" old-1 &&
	git -C full commit-graph write --reachable &&
	test_commit -C full --date "$FUTURE_DATE" future-2 &&
	test_commit -C full --date "$UNIX_EPOCH_ZERO" old-2 &&
	git -C full commit-graph write --reachable --split=no-merge &&
	test_commit -C full extra &&
	git -C full commit-graph write --reachable --split=no-merge &&
	git -C full commit-graph write --reachable &&
	graph_read_expect -C full 16 \
		"generation_data generation_data_overflow extra_edges" &&
	mv full/.git/objects/info/commit-graph commit-graph-upgraded &&
	git -C full commit-graph write --reachable &&
	graph_read_expect -C full 16 \
		"generation_data generation_data_overflow extra_edges" &&
	test_cmp full/.git/objects/info/commit-graph commit-graph-upgraded
'

# the verify tests below expect the commit-graph to contain
# exactly the commits reachable from the commits/8 branch.
# If the file changes the set of commits in the list, then the
# offsets into the binary file will result in different edits
# and the tests will likely break.

test_expect_success 'git commit-graph verify' '
	git -C full rev-parse commits/8 >in &&
	git -C full -c commitGraph.generationVersion=1 commit-graph write \
		--stdin-commits <in &&
	git -C full commit-graph verify >output &&
	graph_read_expect -C full 9 extra_edges 1
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
GRAPH_COMMIT_DATA_WIDTH=$(($HASH_LEN + 16))
GRAPH_COMMIT_DATA_OFFSET=$(($GRAPH_OID_LOOKUP_OFFSET + $HASH_LEN * $NUM_COMMITS))
GRAPH_BYTE_COMMIT_TREE=$GRAPH_COMMIT_DATA_OFFSET
GRAPH_BYTE_COMMIT_PARENT=$(($GRAPH_COMMIT_DATA_OFFSET + $HASH_LEN))
GRAPH_BYTE_COMMIT_EXTRA_PARENT=$(($GRAPH_COMMIT_DATA_OFFSET + $HASH_LEN + 4))
GRAPH_BYTE_COMMIT_WRONG_PARENT=$(($GRAPH_COMMIT_DATA_OFFSET + $HASH_LEN + 3))
GRAPH_BYTE_COMMIT_GENERATION=$(($GRAPH_COMMIT_DATA_OFFSET + $HASH_LEN + 11))
GRAPH_BYTE_COMMIT_GENERATION_LAST=$(($GRAPH_BYTE_COMMIT_GENERATION + $(($NUM_COMMITS - 1)) * $GRAPH_COMMIT_DATA_WIDTH))
GRAPH_BYTE_COMMIT_DATE=$(($GRAPH_COMMIT_DATA_OFFSET + $HASH_LEN + 12))
GRAPH_OCTOPUS_DATA_OFFSET=$(($GRAPH_COMMIT_DATA_OFFSET + \
			     $GRAPH_COMMIT_DATA_WIDTH * $NUM_COMMITS))
GRAPH_BYTE_OCTOPUS=$(($GRAPH_OCTOPUS_DATA_OFFSET + 4))
GRAPH_BYTE_FOOTER=$(($GRAPH_OCTOPUS_DATA_OFFSET + 4 * $NUM_OCTOPUS_EDGES))

corrupt_graph_setup() {
	test_when_finished mv commit-graph-backup full/$objdir/info/commit-graph &&
	cp full/$objdir/info/commit-graph commit-graph-backup &&
	chmod u+w full/$objdir/info/commit-graph
}

corrupt_graph_verify() {
	grepstr=$1
	test_must_fail git -C full commit-graph verify 2>test_err &&
	grep -v "^+" test_err >err &&
	test_grep "$grepstr" err &&
	if test "$2" != "no-copy"
	then
		cp full/$objdir/info/commit-graph commit-graph-pre-write-test
	fi &&
	git -C full status --short &&
	GIT_TEST_COMMIT_GRAPH_DIE_ON_PARSE=true git -C full commit-graph write &&
	chmod u+w full/$objdir/info/commit-graph &&
	git -C full commit-graph verify
}

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
	corrupt_graph_setup &&
	orig_size=$(wc -c <full/$objdir/info/commit-graph) &&
	zero_pos=${4:-${orig_size}} &&
	printf "$data" | dd of="full/$objdir/info/commit-graph" bs=1 seek="$pos" conv=notrunc &&
	dd of="full/$objdir/info/commit-graph" bs=1 seek="$zero_pos" if=/dev/null &&
	test-tool genzeros $(($orig_size - $zero_pos)) >>"full/$objdir/info/commit-graph" &&
	corrupt_graph_verify "$grepstr"

}

test_expect_success POSIXPERM,SANITY 'detect permission problem' '
	corrupt_graph_setup &&
	chmod 000 full/$objdir/info/commit-graph &&
	corrupt_graph_verify "Could not open" "no-copy"
'

test_expect_success 'detect too small' '
	corrupt_graph_setup &&
	echo "a small graph" >full/$objdir/info/commit-graph &&
	corrupt_graph_verify "too small"
'

test_expect_success 'detect bad signature' '
	corrupt_graph_and_verify 0 "\0" \
		"graph signature"
'

test_expect_success 'detect bad version' '
	corrupt_graph_and_verify $GRAPH_BYTE_VERSION "\02" \
		"graph version"
'

test_expect_success 'detect bad hash version' '
	corrupt_graph_and_verify $GRAPH_BYTE_HASH "\03" \
		"hash version"
'

test_expect_success 'detect low chunk count' '
	corrupt_graph_and_verify $GRAPH_BYTE_CHUNK_COUNT "\01" \
		"final chunk has non-zero id"
'

test_expect_success 'detect missing OID fanout chunk' '
	corrupt_graph_and_verify $GRAPH_BYTE_OID_FANOUT_ID "\0" \
		"commit-graph required OID fanout chunk missing or corrupted"
'

test_expect_success 'detect missing OID lookup chunk' '
	corrupt_graph_and_verify $GRAPH_BYTE_OID_LOOKUP_ID "\0" \
		"commit-graph required OID lookup chunk missing or corrupted"
'

test_expect_success 'detect missing commit data chunk' '
	corrupt_graph_and_verify $GRAPH_BYTE_COMMIT_DATA_ID "\0" \
		"commit-graph required commit data chunk missing or corrupted"
'

test_expect_success 'detect incorrect fanout' '
	corrupt_graph_and_verify $GRAPH_BYTE_FANOUT1 "\01" \
		"fanout value"
'

test_expect_success 'detect incorrect fanout final value' '
	corrupt_graph_and_verify $GRAPH_BYTE_FANOUT2 "\01" \
		"OID lookup chunk is the wrong size"
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
		"commit-graph file is too small to hold [0-9]* chunks" \
		$GRAPH_CHUNK_LOOKUP_OFFSET
'

test_expect_success 'detect mixed generation numbers (non-zero to zero)' '
	corrupt_graph_and_verify $GRAPH_BYTE_COMMIT_GENERATION_LAST "\0\0\0\0" \
		"both zero and non-zero generations"
'

test_expect_success 'detect mixed generation numbers (zero to non-zero)' '
	corrupt_graph_and_verify $GRAPH_BYTE_COMMIT_GENERATION "\0\0\0\0" \
		"both zero and non-zero generations"
'

test_expect_success 'git fsck (checks commit-graph when config set to true)' '
	git -C full fsck &&
	corrupt_graph_and_verify $GRAPH_BYTE_FOOTER "\00" \
		"incorrect checksum" &&
	cp commit-graph-pre-write-test full/$objdir/info/commit-graph &&
	test_must_fail git -C full -c core.commitGraph=true fsck
'

test_expect_success 'git fsck (ignores commit-graph when config set to false)' '
	git -C full fsck &&
	corrupt_graph_and_verify $GRAPH_BYTE_FOOTER "\00" \
		"incorrect checksum" &&
	cp commit-graph-pre-write-test full/$objdir/info/commit-graph &&
	git -C full -c core.commitGraph=false fsck
'

test_expect_success 'git fsck (checks commit-graph when config unset)' '
	test_when_finished "git -C full config core.commitGraph true" &&

	git -C full fsck &&
	corrupt_graph_and_verify $GRAPH_BYTE_FOOTER "\00" \
		"incorrect checksum" &&
	test_unconfig -C full core.commitGraph &&
	cp commit-graph-pre-write-test full/$objdir/info/commit-graph &&
	test_must_fail git -C full fsck
'

test_expect_success 'git fsck shows commit-graph output with --progress' '
	git -C "$TRASH_DIRECTORY/full" fsck --progress 2>err &&
	grep "Verifying commits in commit graph" err
'

test_expect_success 'git fsck suppresses commit-graph output with --no-progress' '
	git -C "$TRASH_DIRECTORY/full" fsck --no-progress 2>err &&
	! grep "Verifying commits in commit graph" err
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

test_expect_success 'corrupt commit-graph write (broken parent)' '
	rm -rf repo &&
	git init repo &&
	(
		cd repo &&
		empty="$(git mktree </dev/null)" &&
		cat >broken <<-EOF &&
		tree $empty
		parent $ZERO_OID
		author whatever <whatever@example.com> 1234 -0000
		committer whatever <whatever@example.com> 1234 -0000

		broken commit
		EOF
		broken="$(git hash-object -w -t commit --literally broken)" &&
		git commit-tree -p "$broken" -m "good commit" "$empty" >good &&
		test_must_fail git commit-graph write --stdin-commits \
			<good 2>test_err &&
		test_grep "unable to parse commit" test_err
	)
'

test_expect_success 'corrupt commit-graph write (missing tree)' '
	rm -rf repo &&
	git init repo &&
	(
		cd repo &&
		tree="$(git mktree </dev/null)" &&
		cat >broken <<-EOF &&
		parent $ZERO_OID
		author whatever <whatever@example.com> 1234 -0000
		committer whatever <whatever@example.com> 1234 -0000

		broken commit
		EOF
		broken="$(git hash-object -w -t commit --literally broken)" &&
		git commit-tree -p "$broken" -m "good" "$tree" >good &&
		test_must_fail git commit-graph write --stdin-commits \
			<good 2>test_err &&
		test_grep "unable to parse commit" test_err
	)
'

# We test the overflow-related code with the following repo history:
#
#               4:F - 5:N - 6:U
#              /                \
# 1:U - 2:N - 3:U                M:N
#              \                /
#               7:N - 8:F - 9:N
#
# Here the commits denoted by U have committer date of zero seconds
# since Unix epoch, the commits denoted by N have committer date
# starting from 1112354055 seconds since Unix epoch (default committer
# date for the test suite), and the commits denoted by F have committer
# date of (2 ^ 31 - 2) seconds since Unix epoch.
#
# The largest offset observed is 2 ^ 31, just large enough to overflow.
#

test_expect_success 'set up and verify repo with generation data overflow chunk' '
	UNIX_EPOCH_ZERO="@0 +0000" &&
	FUTURE_DATE="@2147483646 +0000" &&

	git init repo &&
	(
		cd repo &&

		test_commit --date "$UNIX_EPOCH_ZERO" 1 &&
		test_commit 2 &&
		test_commit --date "$UNIX_EPOCH_ZERO" 3 &&
		git commit-graph write --reachable &&
		graph_read_expect 3 generation_data &&
		test_commit --date "$FUTURE_DATE" 4 &&
		test_commit 5 &&
		test_commit --date "$UNIX_EPOCH_ZERO" 6 &&
		git branch left &&
		git reset --hard 3 &&
		test_commit 7 &&
		test_commit --date "$FUTURE_DATE" 8 &&
		test_commit 9 &&
		git branch right &&
		git reset --hard 3 &&
		test_merge M left right &&
		git commit-graph write --reachable &&
		graph_read_expect 10 "generation_data generation_data_overflow" &&
		git commit-graph verify
	)
'

graph_git_behavior 'generation data overflow chunk repo' repo left right

test_expect_success 'overflow during generation version upgrade' '
	git init overflow-v2-upgrade &&
	(
		cd overflow-v2-upgrade &&

		# This commit will have a date at two seconds past the Epoch,
		# and a (v1) generation number of 1, since it is a root commit.
		#
		# The offset will then be computed as 1-2, which will underflow
		# to 2^31, which is greater than the v2 offset small limit of
		# 2^31-1.
		#
		# This is sufficient to need a large offset table for the v2
		# generation numbers.
		test_commit --date "@2 +0000" base &&
		git repack -d &&

		# Test that upgrading from generation v1 to v2 correctly
		# produces the overflow table.
		git -c commitGraph.generationVersion=1 commit-graph write &&
		git -c commitGraph.generationVersion=2 commit-graph write \
			--changed-paths &&

		git rev-list --all
	)
'

corrupt_chunk () {
	graph=full/.git/objects/info/commit-graph &&
	test_when_finished "rm -rf $graph" &&
	git -C full commit-graph write --reachable &&
	corrupt_chunk_file $graph "$@"
}

check_corrupt_chunk () {
	corrupt_chunk "$@" &&
	git -C full -c core.commitGraph=false log >expect.out &&
	git -C full -c core.commitGraph=true log >out 2>err &&
	test_cmp expect.out out
}

test_expect_success 'reader notices too-small oid fanout chunk' '
	# make it big enough that the graph file is plausible,
	# otherwise we hit an earlier check
	check_corrupt_chunk OIDF clear $(printf "000000%02x" $(test_seq 250)) &&
	cat >expect.err <<-\EOF &&
	error: commit-graph oid fanout chunk is wrong size
	error: commit-graph required OID fanout chunk missing or corrupted
	EOF
	test_cmp expect.err err
'

test_expect_success 'reader notices fanout/lookup table mismatch' '
	check_corrupt_chunk OIDF 1020 "FFFFFFFF" &&
	cat >expect.err <<-\EOF &&
	error: commit-graph OID lookup chunk is the wrong size
	error: commit-graph required OID lookup chunk missing or corrupted
	EOF
	test_cmp expect.err err
'

test_expect_success 'reader notices out-of-bounds fanout' '
	# Rather than try to corrupt a specific hash, we will just
	# wreck them all. But we cannot just set them all to 0xFFFFFFFF or
	# similar, as they are used for hi/lo starts in a binary search (so if
	# they are identical, that indicates that the search should abort
	# immediately). Instead, we will give them high values that differ by
	# 2^24, ensuring that any that are used would cause an out-of-bounds
	# read.
	check_corrupt_chunk OIDF 0 $(printf "%02x000000" $(test_seq 0 254)) &&
	cat >expect.err <<-\EOF &&
	error: commit-graph fanout values out of order
	error: commit-graph required OID fanout chunk missing or corrupted
	EOF
	test_cmp expect.err err
'

test_expect_success 'reader notices too-small commit data chunk' '
	check_corrupt_chunk CDAT clear 00000000 &&
	cat >expect.err <<-\EOF &&
	error: commit-graph commit data chunk is wrong size
	error: commit-graph required commit data chunk missing or corrupted
	EOF
	test_cmp expect.err err
'

test_expect_success 'reader notices out-of-bounds extra edge' '
	check_corrupt_chunk EDGE clear &&
	cat >expect.err <<-\EOF &&
	error: commit-graph extra-edges pointer out of bounds
	EOF
	test_cmp expect.err err
'

test_expect_success 'reader notices too-small generations chunk' '
	check_corrupt_chunk GDA2 clear 00000000 &&
	cat >expect.err <<-\EOF &&
	error: commit-graph generations chunk is wrong size
	EOF
	test_cmp expect.err err
'

test_expect_success 'stale commit cannot be parsed when given directly' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit A &&
		test_commit B &&
		git commit-graph write --reachable &&

		oid=$(git rev-parse B) &&
		rm .git/objects/"$(test_oid_to_path "$oid")" &&

		# Verify that it is possible to read the commit from the
		# commit graph when not being paranoid, ...
		git rev-list B &&
		# ... but parsing the commit when double checking that
		# it actually exists in the object database should fail.
		test_must_fail env GIT_COMMIT_GRAPH_PARANOIA=true git rev-list -1 B
	)
'

test_expect_success 'stale commit cannot be parsed when traversing graph' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_commit A &&
		test_commit B &&
		test_commit C &&
		git commit-graph write --reachable &&

		# Corrupt the repository by deleting the intermediate commit
		# object. Commands should notice that this object is absent and
		# thus that the repository is corrupt even if the commit graph
		# exists.
		oid=$(git rev-parse B) &&
		rm .git/objects/"$(test_oid_to_path "$oid")" &&

		# Again, we should be able to parse the commit when not
		# being paranoid about commit graph staleness...
		git rev-parse HEAD~2 &&
		# ... but fail when we are paranoid.
		test_must_fail env GIT_COMMIT_GRAPH_PARANOIA=true git rev-parse HEAD~2 2>error &&
		grep "error: commit $oid exists in commit-graph but not in the object database" error
	)
'

test_done
