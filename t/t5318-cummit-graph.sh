#!/bin/sh

test_description='cummit graph'
. ./test-lib.sh

GIT_TEST_cummit_GRAPH_CHANGED_PATHS=0

test_expect_success 'usage' '
	test_expect_code 129 git cummit-graph write blah 2>err &&
	test_expect_code 129 git cummit-graph write verify
'

test_expect_success 'usage shown without sub-command' '
	test_expect_code 129 git cummit-graph 2>err &&
	! grep error: err
'

test_expect_success 'usage shown with an error on unknown sub-command' '
	cat >expect <<-\EOF &&
	error: unrecognized subcommand: unknown
	EOF
	test_expect_code 129 git cummit-graph unknown 2>stderr &&
	grep error stderr >actual &&
	test_cmp expect actual
'

test_expect_success 'setup full repo' '
	mkdir full &&
	cd "$TRASH_DIRECTORY/full" &&
	git init &&
	git config core.cummitGraph true &&
	objdir=".git/objects"
'

test_expect_success POSIXPERM 'tweak umask for modebit tests' '
	umask 022
'

test_expect_success 'verify graph with no graph file' '
	cd "$TRASH_DIRECTORY/full" &&
	git cummit-graph verify
'

test_expect_success 'write graph with no packs' '
	cd "$TRASH_DIRECTORY/full" &&
	git cummit-graph write --object-dir $objdir &&
	test_path_is_missing $objdir/info/cummit-graph
'

test_expect_success 'exit with correct error on bad input to --stdin-packs' '
	cd "$TRASH_DIRECTORY/full" &&
	echo doesnotexist >in &&
	test_expect_code 1 git cummit-graph write --stdin-packs <in 2>stderr &&
	test_i18ngrep "error adding pack" stderr
'

test_expect_success 'create cummits and repack' '
	cd "$TRASH_DIRECTORY/full" &&
	for i in $(test_seq 3)
	do
		test_cummit $i &&
		git branch cummits/$i || return 1
	done &&
	git repack
'

. "$TEST_DIRECTORY"/lib-cummit-graph.sh

graph_git_behavior 'no graph' full cummits/3 cummits/1

test_expect_success 'exit with correct error on bad input to --stdin-cummits' '
	cd "$TRASH_DIRECTORY/full" &&
	# invalid, non-hex OID
	echo HEAD >in &&
	test_expect_code 1 git cummit-graph write --stdin-cummits <in 2>stderr &&
	test_i18ngrep "unexpected non-hex object ID: HEAD" stderr &&
	# non-existent OID
	echo $ZERO_OID >in &&
	test_expect_code 1 git cummit-graph write --stdin-cummits <in 2>stderr &&
	test_i18ngrep "invalid object" stderr &&
	# valid cummit and tree OID
	git rev-parse HEAD HEAD^{tree} >in &&
	git cummit-graph write --stdin-cummits <in &&
	graph_read_expect 3 generation_data
'

test_expect_success 'write graph' '
	cd "$TRASH_DIRECTORY/full" &&
	git cummit-graph write &&
	test_path_is_file $objdir/info/cummit-graph &&
	graph_read_expect "3" generation_data
'

test_expect_success POSIXPERM 'write graph has correct permissions' '
	test_path_is_file $objdir/info/cummit-graph &&
	echo "-r--r--r--" >expect &&
	test_modebits $objdir/info/cummit-graph >actual &&
	test_cmp expect actual
'

graph_git_behavior 'graph exists' full cummits/3 cummits/1

test_expect_success 'Add more cummits' '
	cd "$TRASH_DIRECTORY/full" &&
	git reset --hard cummits/1 &&
	for i in $(test_seq 4 5)
	do
		test_cummit $i &&
		git branch cummits/$i || return 1
	done &&
	git reset --hard cummits/2 &&
	for i in $(test_seq 6 7)
	do
		test_cummit $i &&
		git branch cummits/$i || return 1
	done &&
	git reset --hard cummits/2 &&
	git merge cummits/4 &&
	git branch merge/1 &&
	git reset --hard cummits/4 &&
	git merge cummits/6 &&
	git branch merge/2 &&
	git reset --hard cummits/3 &&
	git merge cummits/5 cummits/7 &&
	git branch merge/3 &&
	git repack
'

test_expect_success 'cummit-graph write progress off for redirected stderr' '
	cd "$TRASH_DIRECTORY/full" &&
	git cummit-graph write 2>err &&
	test_must_be_empty err
'

test_expect_success 'cummit-graph write force progress on for stderr' '
	cd "$TRASH_DIRECTORY/full" &&
	GIT_PROGRESS_DELAY=0 git cummit-graph write --progress 2>err &&
	test_file_not_empty err
'

test_expect_success 'cummit-graph write with the --no-progress option' '
	cd "$TRASH_DIRECTORY/full" &&
	git cummit-graph write --no-progress 2>err &&
	test_must_be_empty err
'

test_expect_success 'cummit-graph write --stdin-cummits progress off for redirected stderr' '
	cd "$TRASH_DIRECTORY/full" &&
	git rev-parse cummits/5 >in &&
	git cummit-graph write --stdin-cummits <in 2>err &&
	test_must_be_empty err
'

test_expect_success 'cummit-graph write --stdin-cummits force progress on for stderr' '
	cd "$TRASH_DIRECTORY/full" &&
	git rev-parse cummits/5 >in &&
	GIT_PROGRESS_DELAY=0 git cummit-graph write --stdin-cummits --progress <in 2>err &&
	test_i18ngrep "Collecting cummits from input" err
'

test_expect_success 'cummit-graph write --stdin-cummits with the --no-progress option' '
	cd "$TRASH_DIRECTORY/full" &&
	git rev-parse cummits/5 >in &&
	git cummit-graph write --stdin-cummits --no-progress <in 2>err &&
	test_must_be_empty err
'

test_expect_success 'cummit-graph verify progress off for redirected stderr' '
	cd "$TRASH_DIRECTORY/full" &&
	git cummit-graph verify 2>err &&
	test_must_be_empty err
'

test_expect_success 'cummit-graph verify force progress on for stderr' '
	cd "$TRASH_DIRECTORY/full" &&
	GIT_PROGRESS_DELAY=0 git cummit-graph verify --progress 2>err &&
	test_file_not_empty err
'

test_expect_success 'cummit-graph verify with the --no-progress option' '
	cd "$TRASH_DIRECTORY/full" &&
	git cummit-graph verify --no-progress 2>err &&
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
	cd "$TRASH_DIRECTORY/full" &&
	git cummit-graph write &&
	test_path_is_file $objdir/info/cummit-graph &&
	graph_read_expect "10" "generation_data extra_edges"
'

graph_git_behavior 'merge 1 vs 2' full merge/1 merge/2
graph_git_behavior 'merge 1 vs 3' full merge/1 merge/3
graph_git_behavior 'merge 2 vs 3' full merge/2 merge/3

test_expect_success 'Add one more cummit' '
	cd "$TRASH_DIRECTORY/full" &&
	test_cummit 8 &&
	git branch cummits/8 &&
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

graph_git_behavior 'mixed mode, cummit 8 vs merge 1' full cummits/8 merge/1
graph_git_behavior 'mixed mode, cummit 8 vs merge 2' full cummits/8 merge/2

test_expect_success 'write graph with new cummit' '
	cd "$TRASH_DIRECTORY/full" &&
	git cummit-graph write &&
	test_path_is_file $objdir/info/cummit-graph &&
	graph_read_expect "11" "generation_data extra_edges"
'

graph_git_behavior 'full graph, cummit 8 vs merge 1' full cummits/8 merge/1
graph_git_behavior 'full graph, cummit 8 vs merge 2' full cummits/8 merge/2

test_expect_success 'write graph with nothing new' '
	cd "$TRASH_DIRECTORY/full" &&
	git cummit-graph write &&
	test_path_is_file $objdir/info/cummit-graph &&
	graph_read_expect "11" "generation_data extra_edges"
'

graph_git_behavior 'cleared graph, cummit 8 vs merge 1' full cummits/8 merge/1
graph_git_behavior 'cleared graph, cummit 8 vs merge 2' full cummits/8 merge/2

test_expect_success 'build graph from latest pack with closure' '
	cd "$TRASH_DIRECTORY/full" &&
	cat new-idx | git cummit-graph write --stdin-packs &&
	test_path_is_file $objdir/info/cummit-graph &&
	graph_read_expect "9" "generation_data extra_edges"
'

graph_git_behavior 'graph from pack, cummit 8 vs merge 1' full cummits/8 merge/1
graph_git_behavior 'graph from pack, cummit 8 vs merge 2' full cummits/8 merge/2

test_expect_success 'build graph from cummits with closure' '
	cd "$TRASH_DIRECTORY/full" &&
	git tag -a -m "merge" tag/merge merge/2 &&
	git rev-parse tag/merge >cummits-in &&
	git rev-parse merge/1 >>cummits-in &&
	cat cummits-in | git cummit-graph write --stdin-cummits &&
	test_path_is_file $objdir/info/cummit-graph &&
	graph_read_expect "6" "generation_data"
'

graph_git_behavior 'graph from cummits, cummit 8 vs merge 1' full cummits/8 merge/1
graph_git_behavior 'graph from cummits, cummit 8 vs merge 2' full cummits/8 merge/2

test_expect_success 'build graph from cummits with append' '
	cd "$TRASH_DIRECTORY/full" &&
	git rev-parse merge/3 | git cummit-graph write --stdin-cummits --append &&
	test_path_is_file $objdir/info/cummit-graph &&
	graph_read_expect "10" "generation_data extra_edges"
'

graph_git_behavior 'append graph, cummit 8 vs merge 1' full cummits/8 merge/1
graph_git_behavior 'append graph, cummit 8 vs merge 2' full cummits/8 merge/2

test_expect_success 'build graph using --reachable' '
	cd "$TRASH_DIRECTORY/full" &&
	git cummit-graph write --reachable &&
	test_path_is_file $objdir/info/cummit-graph &&
	graph_read_expect "11" "generation_data extra_edges"
'

graph_git_behavior 'append graph, cummit 8 vs merge 1' full cummits/8 merge/1
graph_git_behavior 'append graph, cummit 8 vs merge 2' full cummits/8 merge/2

test_expect_success 'setup bare repo' '
	cd "$TRASH_DIRECTORY" &&
	git clone --bare --no-local full bare &&
	cd bare &&
	git config core.cummitGraph true &&
	baredir="./objects"
'

graph_git_behavior 'bare repo, cummit 8 vs merge 1' bare cummits/8 merge/1
graph_git_behavior 'bare repo, cummit 8 vs merge 2' bare cummits/8 merge/2

test_expect_success 'write graph in bare repo' '
	cd "$TRASH_DIRECTORY/bare" &&
	git cummit-graph write &&
	test_path_is_file $baredir/info/cummit-graph &&
	graph_read_expect "11" "generation_data extra_edges"
'

graph_git_behavior 'bare repo with graph, cummit 8 vs merge 1' bare cummits/8 merge/1
graph_git_behavior 'bare repo with graph, cummit 8 vs merge 2' bare cummits/8 merge/2

test_expect_success 'perform fast-forward merge in full repo' '
	cd "$TRASH_DIRECTORY/full" &&
	git checkout -b merge-5-to-8 cummits/5 &&
	git merge cummits/8 &&
	git show-ref -s merge-5-to-8 >output &&
	git show-ref -s cummits/8 >expect &&
	test_cmp expect output
'

test_expect_success 'check that gc computes cummit-graph' '
	cd "$TRASH_DIRECTORY/full" &&
	git cummit --allow-empty -m "blank" &&
	git cummit-graph write --reachable &&
	cp $objdir/info/cummit-graph cummit-graph-before-gc &&
	git reset --hard HEAD~1 &&
	git config gc.writecummitGraph true &&
	git gc &&
	cp $objdir/info/cummit-graph cummit-graph-after-gc &&
	! test_cmp_bin cummit-graph-before-gc cummit-graph-after-gc &&
	git cummit-graph write --reachable &&
	test_cmp_bin cummit-graph-after-gc $objdir/info/cummit-graph
'

test_expect_success 'replace-objects invalidates cummit-graph' '
	cd "$TRASH_DIRECTORY" &&
	test_when_finished rm -rf replace &&
	git clone full replace &&
	(
		cd replace &&
		git cummit-graph write --reachable &&
		test_path_is_file .git/objects/info/cummit-graph &&
		git replace HEAD~1 HEAD~2 &&
		graph_git_two_modes "cummit-graph verify" &&
		git -c core.cummitGraph=false log >expect &&
		git -c core.cummitGraph=true log >actual &&
		test_cmp expect actual &&
		git cummit-graph write --reachable &&
		git -c core.cummitGraph=false --no-replace-objects log >expect &&
		git -c core.cummitGraph=true --no-replace-objects log >actual &&
		test_cmp expect actual &&
		rm -rf .git/objects/info/cummit-graph &&
		git cummit-graph write --reachable &&
		test_path_is_file .git/objects/info/cummit-graph
	)
'

test_expect_success 'cummit grafts invalidate cummit-graph' '
	cd "$TRASH_DIRECTORY" &&
	test_when_finished rm -rf graft &&
	git clone full graft &&
	(
		cd graft &&
		git cummit-graph write --reachable &&
		test_path_is_file .git/objects/info/cummit-graph &&
		H1=$(git rev-parse --verify HEAD~1) &&
		H3=$(git rev-parse --verify HEAD~3) &&
		echo "$H1 $H3" >.git/info/grafts &&
		git -c core.cummitGraph=false log >expect &&
		git -c core.cummitGraph=true log >actual &&
		test_cmp expect actual &&
		git cummit-graph write --reachable &&
		git -c core.cummitGraph=false --no-replace-objects log >expect &&
		git -c core.cummitGraph=true --no-replace-objects log >actual &&
		test_cmp expect actual &&
		rm -rf .git/objects/info/cummit-graph &&
		git cummit-graph write --reachable &&
		test_path_is_missing .git/objects/info/cummit-graph
	)
'

test_expect_success 'replace-objects invalidates cummit-graph' '
	cd "$TRASH_DIRECTORY" &&
	test_when_finished rm -rf shallow &&
	git clone --depth 2 "file://$TRASH_DIRECTORY/full" shallow &&
	(
		cd shallow &&
		git cummit-graph write --reachable &&
		test_path_is_missing .git/objects/info/cummit-graph &&
		git fetch origin --unshallow &&
		git cummit-graph write --reachable &&
		test_path_is_file .git/objects/info/cummit-graph
	)
'

test_expect_success 'warn on improper hash version' '
	git init --object-format=sha1 sha1 &&
	(
		cd sha1 &&
		test_cummit 1 &&
		git cummit-graph write --reachable &&
		mv .git/objects/info/cummit-graph ../cg-sha1
	) &&
	git init --object-format=sha256 sha256 &&
	(
		cd sha256 &&
		test_cummit 1 &&
		git cummit-graph write --reachable &&
		mv .git/objects/info/cummit-graph ../cg-sha256
	) &&
	(
		cd sha1 &&
		mv ../cg-sha256 .git/objects/info/cummit-graph &&
		git log -1 2>err &&
		test_i18ngrep "cummit-graph hash version 2 does not match version 1" err
	) &&
	(
		cd sha256 &&
		mv ../cg-sha1 .git/objects/info/cummit-graph &&
		git log -1 2>err &&
		test_i18ngrep "cummit-graph hash version 1 does not match version 2" err
	)
'

test_expect_success TIME_IS_64BIT,TIME_T_IS_64BIT 'lower layers have overflow chunk' '
	cd "$TRASH_DIRECTORY/full" &&
	UNIX_EPOCH_ZERO="@0 +0000" &&
	FUTURE_DATE="@4147483646 +0000" &&
	rm -f .git/objects/info/cummit-graph &&
	test_cummit --date "$FUTURE_DATE" future-1 &&
	test_cummit --date "$UNIX_EPOCH_ZERO" old-1 &&
	git cummit-graph write --reachable &&
	test_cummit --date "$FUTURE_DATE" future-2 &&
	test_cummit --date "$UNIX_EPOCH_ZERO" old-2 &&
	git cummit-graph write --reachable --split=no-merge &&
	test_cummit extra &&
	git cummit-graph write --reachable --split=no-merge &&
	git cummit-graph write --reachable &&
	graph_read_expect 16 "generation_data generation_data_overflow extra_edges" &&
	mv .git/objects/info/cummit-graph cummit-graph-upgraded &&
	git cummit-graph write --reachable &&
	graph_read_expect 16 "generation_data generation_data_overflow extra_edges" &&
	test_cmp .git/objects/info/cummit-graph cummit-graph-upgraded
'

# the verify tests below expect the cummit-graph to contain
# exactly the cummits reachable from the cummits/8 branch.
# If the file changes the set of cummits in the list, then the
# offsets into the binary file will result in different edits
# and the tests will likely break.

test_expect_success 'git cummit-graph verify' '
	cd "$TRASH_DIRECTORY/full" &&
	git rev-parse cummits/8 | git -c cummitGraph.generationVersion=1 cummit-graph write --stdin-cummits &&
	git cummit-graph verify >output &&
	graph_read_expect 9 extra_edges 1
'

NUM_cummitS=9
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
GRAPH_BYTE_cummit_DATA_ID=$(($GRAPH_CHUNK_LOOKUP_OFFSET + \
			     2 * $GRAPH_CHUNK_LOOKUP_WIDTH))
GRAPH_FANOUT_OFFSET=$(($GRAPH_CHUNK_LOOKUP_OFFSET + \
		       $GRAPH_CHUNK_LOOKUP_WIDTH * $GRAPH_CHUNK_LOOKUP_ROWS))
GRAPH_BYTE_FANOUT1=$(($GRAPH_FANOUT_OFFSET + 4 * 4))
GRAPH_BYTE_FANOUT2=$(($GRAPH_FANOUT_OFFSET + 4 * 255))
GRAPH_OID_LOOKUP_OFFSET=$(($GRAPH_FANOUT_OFFSET + 4 * 256))
GRAPH_BYTE_OID_LOOKUP_ORDER=$(($GRAPH_OID_LOOKUP_OFFSET + $HASH_LEN * 8))
GRAPH_BYTE_OID_LOOKUP_MISSING=$(($GRAPH_OID_LOOKUP_OFFSET + $HASH_LEN * 4 + 10))
GRAPH_cummit_DATA_OFFSET=$(($GRAPH_OID_LOOKUP_OFFSET + $HASH_LEN * $NUM_cummitS))
GRAPH_BYTE_cummit_TREE=$GRAPH_cummit_DATA_OFFSET
GRAPH_BYTE_cummit_PARENT=$(($GRAPH_cummit_DATA_OFFSET + $HASH_LEN))
GRAPH_BYTE_cummit_EXTRA_PARENT=$(($GRAPH_cummit_DATA_OFFSET + $HASH_LEN + 4))
GRAPH_BYTE_cummit_WRONG_PARENT=$(($GRAPH_cummit_DATA_OFFSET + $HASH_LEN + 3))
GRAPH_BYTE_cummit_GENERATION=$(($GRAPH_cummit_DATA_OFFSET + $HASH_LEN + 11))
GRAPH_BYTE_cummit_DATE=$(($GRAPH_cummit_DATA_OFFSET + $HASH_LEN + 12))
GRAPH_cummit_DATA_WIDTH=$(($HASH_LEN + 16))
GRAPH_OCTOPUS_DATA_OFFSET=$(($GRAPH_cummit_DATA_OFFSET + \
			     $GRAPH_cummit_DATA_WIDTH * $NUM_cummitS))
GRAPH_BYTE_OCTOPUS=$(($GRAPH_OCTOPUS_DATA_OFFSET + 4))
GRAPH_BYTE_FOOTER=$(($GRAPH_OCTOPUS_DATA_OFFSET + 4 * $NUM_OCTOPUS_EDGES))

corrupt_graph_setup() {
	cd "$TRASH_DIRECTORY/full" &&
	test_when_finished mv cummit-graph-backup $objdir/info/cummit-graph &&
	cp $objdir/info/cummit-graph cummit-graph-backup &&
	chmod u+w $objdir/info/cummit-graph
}

corrupt_graph_verify() {
	grepstr=$1
	test_must_fail git cummit-graph verify 2>test_err &&
	grep -v "^+" test_err >err &&
	test_i18ngrep "$grepstr" err &&
	if test "$2" != "no-copy"
	then
		cp $objdir/info/cummit-graph cummit-graph-pre-write-test
	fi &&
	git status --short &&
	GIT_TEST_cummit_GRAPH_DIE_ON_PARSE=true git cummit-graph write &&
	chmod u+w $objdir/info/cummit-graph &&
	git cummit-graph verify
}

# usage: corrupt_graph_and_verify <position> <data> <string> [<zero_pos>]
# Manipulates the cummit-graph file at the position
# by inserting the data, optionally zeroing the file
# starting at <zero_pos>, then runs 'git cummit-graph verify'
# and places the output in the file 'err'. Test 'err' for
# the given string.
corrupt_graph_and_verify() {
	pos=$1
	data="${2:-\0}"
	grepstr=$3
	corrupt_graph_setup &&
	orig_size=$(wc -c < $objdir/info/cummit-graph) &&
	zero_pos=${4:-${orig_size}} &&
	printf "$data" | dd of="$objdir/info/cummit-graph" bs=1 seek="$pos" conv=notrunc &&
	dd of="$objdir/info/cummit-graph" bs=1 seek="$zero_pos" if=/dev/null &&
	test-tool genzeros $(($orig_size - $zero_pos)) >>"$objdir/info/cummit-graph" &&
	corrupt_graph_verify "$grepstr"

}

test_expect_success POSIXPERM,SANITY 'detect permission problem' '
	corrupt_graph_setup &&
	chmod 000 $objdir/info/cummit-graph &&
	corrupt_graph_verify "Could not open" "no-copy"
'

test_expect_success 'detect too small' '
	corrupt_graph_setup &&
	echo "a small graph" >$objdir/info/cummit-graph &&
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
		"missing the OID Fanout chunk"
'

test_expect_success 'detect missing OID lookup chunk' '
	corrupt_graph_and_verify $GRAPH_BYTE_OID_LOOKUP_ID "\0" \
		"missing the OID Lookup chunk"
'

test_expect_success 'detect missing cummit data chunk' '
	corrupt_graph_and_verify $GRAPH_BYTE_cummit_DATA_ID "\0" \
		"missing the cummit Data chunk"
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
	corrupt_graph_and_verify $GRAPH_BYTE_cummit_TREE "\01" \
		"root tree OID for cummit"
'

test_expect_success 'detect incorrect parent int-id' '
	corrupt_graph_and_verify $GRAPH_BYTE_cummit_PARENT "\01" \
		"invalid parent"
'

test_expect_success 'detect extra parent int-id' '
	corrupt_graph_and_verify $GRAPH_BYTE_cummit_EXTRA_PARENT "\00" \
		"is too long"
'

test_expect_success 'detect wrong parent' '
	corrupt_graph_and_verify $GRAPH_BYTE_cummit_WRONG_PARENT "\01" \
		"cummit-graph parent for"
'

test_expect_success 'detect incorrect generation number' '
	corrupt_graph_and_verify $GRAPH_BYTE_cummit_GENERATION "\070" \
		"generation for cummit"
'

test_expect_success 'detect incorrect generation number' '
	corrupt_graph_and_verify $GRAPH_BYTE_cummit_GENERATION "\01" \
		"non-zero generation number"
'

test_expect_success 'detect incorrect cummit date' '
	corrupt_graph_and_verify $GRAPH_BYTE_cummit_DATE "\01" \
		"cummit date"
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
		"cummit-graph file is too small to hold [0-9]* chunks" \
		$GRAPH_CHUNK_LOOKUP_OFFSET
'

test_expect_success 'git fsck (checks cummit-graph when config set to true)' '
	cd "$TRASH_DIRECTORY/full" &&
	git fsck &&
	corrupt_graph_and_verify $GRAPH_BYTE_FOOTER "\00" \
		"incorrect checksum" &&
	cp cummit-graph-pre-write-test $objdir/info/cummit-graph &&
	test_must_fail git -c core.cummitGraph=true fsck
'

test_expect_success 'git fsck (ignores cummit-graph when config set to false)' '
	cd "$TRASH_DIRECTORY/full" &&
	git fsck &&
	corrupt_graph_and_verify $GRAPH_BYTE_FOOTER "\00" \
		"incorrect checksum" &&
	cp cummit-graph-pre-write-test $objdir/info/cummit-graph &&
	git -c core.cummitGraph=false fsck
'

test_expect_success 'git fsck (checks cummit-graph when config unset)' '
	cd "$TRASH_DIRECTORY/full" &&
	test_when_finished "git config core.cummitGraph true" &&

	git fsck &&
	corrupt_graph_and_verify $GRAPH_BYTE_FOOTER "\00" \
		"incorrect checksum" &&
	test_unconfig core.cummitGraph &&
	cp cummit-graph-pre-write-test $objdir/info/cummit-graph &&
	test_must_fail git fsck
'

test_expect_success 'setup non-the_repository tests' '
	rm -rf repo &&
	git init repo &&
	test_cummit -C repo one &&
	test_cummit -C repo two &&
	git -C repo config core.cummitGraph true &&
	git -C repo rev-parse two | \
		git -C repo cummit-graph write --stdin-cummits
'

test_expect_success 'parse_cummit_in_graph works for non-the_repository' '
	test-tool repository parse_cummit_in_graph \
		repo/.git repo "$(git -C repo rev-parse two)" >actual &&
	{
		git -C repo log --pretty=format:"%ct " -1 &&
		git -C repo rev-parse one
	} >expect &&
	test_cmp expect actual &&

	test-tool repository parse_cummit_in_graph \
		repo/.git repo "$(git -C repo rev-parse one)" >actual &&
	git -C repo log --pretty="%ct" -1 one >expect &&
	test_cmp expect actual
'

test_expect_success 'get_cummit_tree_in_graph works for non-the_repository' '
	test-tool repository get_cummit_tree_in_graph \
		repo/.git repo "$(git -C repo rev-parse two)" >actual &&
	git -C repo rev-parse two^{tree} >expect &&
	test_cmp expect actual &&

	test-tool repository get_cummit_tree_in_graph \
		repo/.git repo "$(git -C repo rev-parse one)" >actual &&
	git -C repo rev-parse one^{tree} >expect &&
	test_cmp expect actual
'

test_expect_success 'corrupt cummit-graph write (broken parent)' '
	rm -rf repo &&
	git init repo &&
	(
		cd repo &&
		empty="$(git mktree </dev/null)" &&
		cat >broken <<-EOF &&
		tree $empty
		parent $ZERO_OID
		author whatever <whatever@example.com> 1234 -0000
		cummitter whatever <whatever@example.com> 1234 -0000

		broken cummit
		EOF
		broken="$(git hash-object -w -t cummit --literally broken)" &&
		git cummit-tree -p "$broken" -m "good cummit" "$empty" >good &&
		test_must_fail git cummit-graph write --stdin-cummits \
			<good 2>test_err &&
		test_i18ngrep "unable to parse cummit" test_err
	)
'

test_expect_success 'corrupt cummit-graph write (missing tree)' '
	rm -rf repo &&
	git init repo &&
	(
		cd repo &&
		tree="$(git mktree </dev/null)" &&
		cat >broken <<-EOF &&
		parent $ZERO_OID
		author whatever <whatever@example.com> 1234 -0000
		cummitter whatever <whatever@example.com> 1234 -0000

		broken cummit
		EOF
		broken="$(git hash-object -w -t cummit --literally broken)" &&
		git cummit-tree -p "$broken" -m "good" "$tree" >good &&
		test_must_fail git cummit-graph write --stdin-cummits \
			<good 2>test_err &&
		test_i18ngrep "unable to parse cummit" test_err
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
# Here the cummits denoted by U have cummitter date of zero seconds
# since Unix epoch, the cummits denoted by N have cummitter date
# starting from 1112354055 seconds since Unix epoch (default cummitter
# date for the test suite), and the cummits denoted by F have cummitter
# date of (2 ^ 31 - 2) seconds since Unix epoch.
#
# The largest offset observed is 2 ^ 31, just large enough to overflow.
#

test_expect_success 'set up and verify repo with generation data overflow chunk' '
	objdir=".git/objects" &&
	UNIX_EPOCH_ZERO="@0 +0000" &&
	FUTURE_DATE="@2147483646 +0000" &&
	cd "$TRASH_DIRECTORY" &&
	mkdir repo &&
	cd repo &&
	git init &&
	test_cummit --date "$UNIX_EPOCH_ZERO" 1 &&
	test_cummit 2 &&
	test_cummit --date "$UNIX_EPOCH_ZERO" 3 &&
	git cummit-graph write --reachable &&
	graph_read_expect 3 generation_data &&
	test_cummit --date "$FUTURE_DATE" 4 &&
	test_cummit 5 &&
	test_cummit --date "$UNIX_EPOCH_ZERO" 6 &&
	git branch left &&
	git reset --hard 3 &&
	test_cummit 7 &&
	test_cummit --date "$FUTURE_DATE" 8 &&
	test_cummit 9 &&
	git branch right &&
	git reset --hard 3 &&
	test_merge M left right &&
	git cummit-graph write --reachable &&
	graph_read_expect 10 "generation_data generation_data_overflow" &&
	git cummit-graph verify
'

graph_git_behavior 'generation data overflow chunk repo' repo left right

test_done
