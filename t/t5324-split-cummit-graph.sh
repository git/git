#!/bin/sh

test_description='split cummit graph'
. ./test-lib.sh

BUT_TEST_CUMMIT_GRAPH=0
BUT_TEST_CUMMIT_GRAPH_CHANGED_PATHS=0

test_expect_success 'setup repo' '
	but init &&
	but config core.cummitGraph true &&
	but config gc.writecummitGraph false &&
	infodir=".but/objects/info" &&
	graphdir="$infodir/cummit-graphs" &&
	test_oid_cache <<-EOM
	shallow sha1:2132
	shallow sha256:2436

	base sha1:1408
	base sha256:1528

	oid_version sha1:1
	oid_version sha256:2
	EOM
'

graph_read_expect() {
	NUM_BASE=0
	if test ! -z $2
	then
		NUM_BASE=$2
	fi
	OPTIONS=
	if test -z "$3"
	then
		OPTIONS=" read_generation_data"
	fi
	cat >expect <<- EOF
	header: 43475048 1 $(test_oid oid_version) 4 $NUM_BASE
	num_cummits: $1
	chunks: oid_fanout oid_lookup cummit_metadata generation_data
	options:$OPTIONS
	EOF
	test-tool read-graph >output &&
	test_cmp expect output
}

test_expect_success POSIXPERM 'tweak umask for modebit tests' '
	umask 022
'

test_expect_success 'create cummits and write cummit-graph' '
	for i in $(test_seq 3)
	do
		test_cummit $i &&
		but branch cummits/$i || return 1
	done &&
	but cummit-graph write --reachable &&
	test_path_is_file $infodir/cummit-graph &&
	graph_read_expect 3
'

graph_but_two_modes() {
	but ${2:+ -C "$2"} -c core.cummitGraph=true $1 >output &&
	but ${2:+ -C "$2"} -c core.cummitGraph=false $1 >expect &&
	test_cmp expect output
}

graph_but_behavior() {
	MSG=$1
	BRANCH=$2
	COMPARE=$3
	DIR=$4
	test_expect_success "check normal but operations: $MSG" '
		graph_but_two_modes "log --oneline $BRANCH" "$DIR" &&
		graph_but_two_modes "log --topo-order $BRANCH" "$DIR" &&
		graph_but_two_modes "log --graph $COMPARE..$BRANCH" "$DIR" &&
		graph_but_two_modes "branch -vv" "$DIR" &&
		graph_but_two_modes "merge-base -a $BRANCH $COMPARE" "$DIR"
	'
}

graph_but_behavior 'graph exists' cummits/3 cummits/1

verify_chain_files_exist() {
	for hash in $(cat $1/cummit-graph-chain)
	do
		test_path_is_file $1/graph-$hash.graph || return 1
	done
}

test_expect_success 'add more cummits, and write a new base graph' '
	but reset --hard cummits/1 &&
	for i in $(test_seq 4 5)
	do
		test_cummit $i &&
		but branch cummits/$i || return 1
	done &&
	but reset --hard cummits/2 &&
	for i in $(test_seq 6 10)
	do
		test_cummit $i &&
		but branch cummits/$i || return 1
	done &&
	but reset --hard cummits/2 &&
	but merge cummits/4 &&
	but branch merge/1 &&
	but reset --hard cummits/4 &&
	but merge cummits/6 &&
	but branch merge/2 &&
	but cummit-graph write --reachable &&
	graph_read_expect 12
'

test_expect_success 'fork and fail to base a chain on a cummit-graph file' '
	test_when_finished rm -rf fork &&
	but clone . fork &&
	(
		cd fork &&
		rm .but/objects/info/cummit-graph &&
		echo "$(pwd)/../.but/objects" >.but/objects/info/alternates &&
		test_cummit new-cummit &&
		but cummit-graph write --reachable --split &&
		test_path_is_file $graphdir/cummit-graph-chain &&
		test_line_count = 1 $graphdir/cummit-graph-chain &&
		verify_chain_files_exist $graphdir
	)
'

test_expect_success 'add three more cummits, write a tip graph' '
	but reset --hard cummits/3 &&
	but merge merge/1 &&
	but merge cummits/5 &&
	but merge merge/2 &&
	but branch merge/3 &&
	but cummit-graph write --reachable --split &&
	test_path_is_missing $infodir/cummit-graph &&
	test_path_is_file $graphdir/cummit-graph-chain &&
	ls $graphdir/graph-*.graph >graph-files &&
	test_line_count = 2 graph-files &&
	verify_chain_files_exist $graphdir
'

graph_but_behavior 'split cummit-graph: merge 3 vs 2' merge/3 merge/2

test_expect_success 'add one cummit, write a tip graph' '
	test_cummit 11 &&
	but branch cummits/11 &&
	but cummit-graph write --reachable --split &&
	test_path_is_missing $infodir/cummit-graph &&
	test_path_is_file $graphdir/cummit-graph-chain &&
	ls $graphdir/graph-*.graph >graph-files &&
	test_line_count = 3 graph-files &&
	verify_chain_files_exist $graphdir
'

graph_but_behavior 'three-layer cummit-graph: cummit 11 vs 6' cummits/11 cummits/6

test_expect_success 'add one cummit, write a merged graph' '
	test_cummit 12 &&
	but branch cummits/12 &&
	but cummit-graph write --reachable --split &&
	test_path_is_file $graphdir/cummit-graph-chain &&
	test_line_count = 2 $graphdir/cummit-graph-chain &&
	ls $graphdir/graph-*.graph >graph-files &&
	test_line_count = 2 graph-files &&
	verify_chain_files_exist $graphdir
'

graph_but_behavior 'merged cummit-graph: cummit 12 vs 6' cummits/12 cummits/6

test_expect_success 'create fork and chain across alternate' '
	but clone . fork &&
	(
		cd fork &&
		but config core.cummitGraph true &&
		rm -rf $graphdir &&
		echo "$(pwd)/../.but/objects" >.but/objects/info/alternates &&
		test_cummit 13 &&
		but branch cummits/13 &&
		but cummit-graph write --reachable --split &&
		test_path_is_file $graphdir/cummit-graph-chain &&
		test_line_count = 3 $graphdir/cummit-graph-chain &&
		ls $graphdir/graph-*.graph >graph-files &&
		test_line_count = 1 graph-files &&
		but -c core.cummitGraph=true  rev-list HEAD >expect &&
		but -c core.cummitGraph=false rev-list HEAD >actual &&
		test_cmp expect actual &&
		test_cummit 14 &&
		but cummit-graph write --reachable --split --object-dir=.but/objects/ &&
		test_line_count = 3 $graphdir/cummit-graph-chain &&
		ls $graphdir/graph-*.graph >graph-files &&
		test_line_count = 1 graph-files
	)
'

if test -d fork
then
	graph_but_behavior 'alternate: cummit 13 vs 6' cummits/13 origin/cummits/6 "fork"
fi

test_expect_success 'test merge stragety constants' '
	but clone . merge-2 &&
	(
		cd merge-2 &&
		but config core.cummitGraph true &&
		test_line_count = 2 $graphdir/cummit-graph-chain &&
		test_cummit 14 &&
		but cummit-graph write --reachable --split --size-multiple=2 &&
		test_line_count = 3 $graphdir/cummit-graph-chain

	) &&
	but clone . merge-10 &&
	(
		cd merge-10 &&
		but config core.cummitGraph true &&
		test_line_count = 2 $graphdir/cummit-graph-chain &&
		test_cummit 14 &&
		but cummit-graph write --reachable --split --size-multiple=10 &&
		test_line_count = 1 $graphdir/cummit-graph-chain &&
		ls $graphdir/graph-*.graph >graph-files &&
		test_line_count = 1 graph-files
	) &&
	but clone . merge-10-expire &&
	(
		cd merge-10-expire &&
		but config core.cummitGraph true &&
		test_line_count = 2 $graphdir/cummit-graph-chain &&
		test_cummit 15 &&
		touch $graphdir/to-delete.graph $graphdir/to-keep.graph &&
		test-tool chmtime =1546362000 $graphdir/to-delete.graph &&
		test-tool chmtime =1546362001 $graphdir/to-keep.graph &&
		but cummit-graph write --reachable --split --size-multiple=10 \
			--expire-time="2019-01-01 12:00 -05:00" &&
		test_line_count = 1 $graphdir/cummit-graph-chain &&
		test_path_is_missing $graphdir/to-delete.graph &&
		test_path_is_file $graphdir/to-keep.graph &&
		ls $graphdir/graph-*.graph >graph-files &&
		test_line_count = 3 graph-files
	) &&
	but clone --no-hardlinks . max-cummits &&
	(
		cd max-cummits &&
		but config core.cummitGraph true &&
		test_line_count = 2 $graphdir/cummit-graph-chain &&
		test_cummit 16 &&
		test_cummit 17 &&
		but cummit-graph write --reachable --split --max-cummits=1 &&
		test_line_count = 1 $graphdir/cummit-graph-chain &&
		ls $graphdir/graph-*.graph >graph-files &&
		test_line_count = 1 graph-files
	)
'

test_expect_success 'remove cummit-graph-chain file after flattening' '
	but clone . flatten &&
	(
		cd flatten &&
		test_line_count = 2 $graphdir/cummit-graph-chain &&
		but cummit-graph write --reachable &&
		test_path_is_missing $graphdir/cummit-graph-chain &&
		ls $graphdir >graph-files &&
		test_line_count = 0 graph-files
	)
'

corrupt_file() {
	file=$1
	pos=$2
	data="${3:-\0}"
	chmod a+w "$file" &&
	printf "$data" | dd of="$file" bs=1 seek="$pos" conv=notrunc
}

test_expect_success 'verify hashes along chain, even in shallow' '
	but clone --no-hardlinks . verify &&
	(
		cd verify &&
		but cummit-graph verify &&
		base_file=$graphdir/graph-$(head -n 1 $graphdir/cummit-graph-chain).graph &&
		corrupt_file "$base_file" $(test_oid shallow) "\01" &&
		test_must_fail but cummit-graph verify --shallow 2>test_err &&
		grep -v "^+" test_err >err &&
		test_i18ngrep "incorrect checksum" err
	)
'

test_expect_success 'verify --shallow does not check base contents' '
	but clone --no-hardlinks . verify-shallow &&
	(
		cd verify-shallow &&
		but cummit-graph verify &&
		base_file=$graphdir/graph-$(head -n 1 $graphdir/cummit-graph-chain).graph &&
		corrupt_file "$base_file" 1000 "\01" &&
		but cummit-graph verify --shallow &&
		test_must_fail but cummit-graph verify 2>test_err &&
		grep -v "^+" test_err >err &&
		test_i18ngrep "incorrect checksum" err
	)
'

test_expect_success 'warn on base graph chunk incorrect' '
	but clone --no-hardlinks . base-chunk &&
	(
		cd base-chunk &&
		but cummit-graph verify &&
		base_file=$graphdir/graph-$(tail -n 1 $graphdir/cummit-graph-chain).graph &&
		corrupt_file "$base_file" $(test_oid base) "\01" &&
		but cummit-graph verify --shallow 2>test_err &&
		grep -v "^+" test_err >err &&
		test_i18ngrep "cummit-graph chain does not match" err
	)
'

test_expect_success 'verify after cummit-graph-chain corruption' '
	but clone --no-hardlinks . verify-chain &&
	(
		cd verify-chain &&
		corrupt_file "$graphdir/cummit-graph-chain" 60 "G" &&
		but cummit-graph verify 2>test_err &&
		grep -v "^+" test_err >err &&
		test_i18ngrep "invalid cummit-graph chain" err &&
		corrupt_file "$graphdir/cummit-graph-chain" 60 "A" &&
		but cummit-graph verify 2>test_err &&
		grep -v "^+" test_err >err &&
		test_i18ngrep "unable to find all cummit-graph files" err
	)
'

test_expect_success 'verify across alternates' '
	but clone --no-hardlinks . verify-alt &&
	(
		cd verify-alt &&
		rm -rf $graphdir &&
		altdir="$(pwd)/../.but/objects" &&
		echo "$altdir" >.but/objects/info/alternates &&
		but cummit-graph verify --object-dir="$altdir/" &&
		test_cummit extra &&
		but cummit-graph write --reachable --split &&
		tip_file=$graphdir/graph-$(tail -n 1 $graphdir/cummit-graph-chain).graph &&
		corrupt_file "$tip_file" 100 "\01" &&
		test_must_fail but cummit-graph verify --shallow 2>test_err &&
		grep -v "^+" test_err >err &&
		test_i18ngrep "cummit-graph has incorrect fanout value" err
	)
'

test_expect_success 'add octopus merge' '
	but reset --hard cummits/10 &&
	but merge cummits/3 cummits/4 &&
	but branch merge/octopus &&
	but cummit-graph write --reachable --split &&
	but cummit-graph verify --progress 2>err &&
	test_line_count = 3 err &&
	test_i18ngrep ! warning err &&
	test_line_count = 3 $graphdir/cummit-graph-chain
'

graph_but_behavior 'graph exists' merge/octopus cummits/12

test_expect_success 'split across alternate where alternate is not split' '
	but cummit-graph write --reachable &&
	test_path_is_file .but/objects/info/cummit-graph &&
	cp .but/objects/info/cummit-graph . &&
	but clone --no-hardlinks . alt-split &&
	(
		cd alt-split &&
		rm -f .but/objects/info/cummit-graph &&
		echo "$(pwd)"/../.but/objects >.but/objects/info/alternates &&
		test_cummit 18 &&
		but cummit-graph write --reachable --split &&
		test_line_count = 1 $graphdir/cummit-graph-chain
	) &&
	test_cmp cummit-graph .but/objects/info/cummit-graph
'

test_expect_success '--split=no-merge always writes an incremental' '
	test_when_finished rm -rf a b &&
	rm -rf $graphdir $infodir/cummit-graph &&
	but reset --hard cummits/2 &&
	but rev-list HEAD~1 >a &&
	but rev-list HEAD >b &&
	but cummit-graph write --split --stdin-cummits <a &&
	but cummit-graph write --split=no-merge --stdin-cummits <b &&
	test_line_count = 2 $graphdir/cummit-graph-chain
'

test_expect_success '--split=replace replaces the chain' '
	rm -rf $graphdir $infodir/cummit-graph &&
	but reset --hard cummits/3 &&
	but rev-list -1 HEAD~2 >a &&
	but rev-list -1 HEAD~1 >b &&
	but rev-list -1 HEAD >c &&
	but cummit-graph write --split=no-merge --stdin-cummits <a &&
	but cummit-graph write --split=no-merge --stdin-cummits <b &&
	but cummit-graph write --split=no-merge --stdin-cummits <c &&
	test_line_count = 3 $graphdir/cummit-graph-chain &&
	but cummit-graph write --stdin-cummits --split=replace <b &&
	test_path_is_missing $infodir/cummit-graph &&
	test_path_is_file $graphdir/cummit-graph-chain &&
	ls $graphdir/graph-*.graph >graph-files &&
	test_line_count = 1 graph-files &&
	verify_chain_files_exist $graphdir &&
	graph_read_expect 2
'

test_expect_success ULIMIT_FILE_DESCRIPTORS 'handles file descriptor exhaustion' '
	but init ulimit &&
	(
		cd ulimit &&
		for i in $(test_seq 64)
		do
			test_cummit $i &&
			run_with_limited_open_files test_might_fail but cummit-graph write \
				--split=no-merge --reachable || return 1
		done
	)
'

while read mode modebits
do
	test_expect_success POSIXPERM "split cummit-graph respects core.sharedrepository $mode" '
		rm -rf $graphdir $infodir/cummit-graph &&
		but reset --hard cummits/1 &&
		test_config core.sharedrepository "$mode" &&
		but cummit-graph write --split --reachable &&
		ls $graphdir/graph-*.graph >graph-files &&
		test_line_count = 1 graph-files &&
		echo "$modebits" >expect &&
		test_modebits $graphdir/graph-*.graph >actual &&
		test_cmp expect actual &&
		test_modebits $graphdir/cummit-graph-chain >actual &&
		test_cmp expect actual
	'
done <<\EOF
0666 -r--r--r--
0600 -r--------
EOF

test_expect_success '--split=replace with partial Bloom data' '
	rm -rf $graphdir $infodir/cummit-graph &&
	but reset --hard cummits/3 &&
	but rev-list -1 HEAD~2 >a &&
	but rev-list -1 HEAD~1 >b &&
	but cummit-graph write --split=no-merge --stdin-cummits --changed-paths <a &&
	but cummit-graph write --split=no-merge --stdin-cummits <b &&
	but cummit-graph write --split=replace --stdin-cummits --changed-paths <c &&
	ls $graphdir/graph-*.graph >graph-files &&
	test_line_count = 1 graph-files &&
	verify_chain_files_exist $graphdir
'

test_expect_success 'prevent regression for duplicate cummits across layers' '
	but init dup &&
	but -C dup cummit --allow-empty -m one &&
	but -C dup -c core.cummitGraph=false cummit-graph write --split=no-merge --reachable 2>err &&
	test_i18ngrep "attempting to write a cummit-graph" err &&
	but -C dup cummit-graph write --split=no-merge --reachable &&
	but -C dup cummit --allow-empty -m two &&
	but -C dup cummit-graph write --split=no-merge --reachable &&
	but -C dup cummit --allow-empty -m three &&
	but -C dup cummit-graph write --split --reachable &&
	but -C dup cummit-graph verify
'

NUM_FIRST_LAYER_CUMMITS=64
NUM_SECOND_LAYER_CUMMITS=16
NUM_THIRD_LAYER_CUMMITS=7
NUM_FOURTH_LAYER_CUMMITS=8
NUM_FIFTH_LAYER_CUMMITS=16
SECOND_LAYER_SEQUENCE_START=$(($NUM_FIRST_LAYER_CUMMITS + 1))
SECOND_LAYER_SEQUENCE_END=$(($SECOND_LAYER_SEQUENCE_START + $NUM_SECOND_LAYER_CUMMITS - 1))
THIRD_LAYER_SEQUENCE_START=$(($SECOND_LAYER_SEQUENCE_END + 1))
THIRD_LAYER_SEQUENCE_END=$(($THIRD_LAYER_SEQUENCE_START + $NUM_THIRD_LAYER_CUMMITS - 1))
FOURTH_LAYER_SEQUENCE_START=$(($THIRD_LAYER_SEQUENCE_END + 1))
FOURTH_LAYER_SEQUENCE_END=$(($FOURTH_LAYER_SEQUENCE_START + $NUM_FOURTH_LAYER_CUMMITS - 1))
FIFTH_LAYER_SEQUENCE_START=$(($FOURTH_LAYER_SEQUENCE_END + 1))
FIFTH_LAYER_SEQUENCE_END=$(($FIFTH_LAYER_SEQUENCE_START + $NUM_FIFTH_LAYER_CUMMITS - 1))

# Current split graph chain:
#
#     16 cummits (No GDAT)
# ------------------------
#     64 cummits (GDAT)
#
test_expect_success 'setup repo for mixed generation cummit-graph-chain' '
	graphdir=".but/objects/info/cummit-graphs" &&
	test_oid_cache <<-EOF &&
	oid_version sha1:1
	oid_version sha256:2
	EOF
	but init mixed &&
	(
		cd mixed &&
		but config core.cummitGraph true &&
		but config gc.writecummitGraph false &&
		for i in $(test_seq $NUM_FIRST_LAYER_CUMMITS)
		do
			test_cummit $i &&
			but branch cummits/$i || return 1
		done &&
		but -c cummitGraph.generationVersion=2 cummit-graph write --reachable --split &&
		graph_read_expect $NUM_FIRST_LAYER_CUMMITS &&
		test_line_count = 1 $graphdir/cummit-graph-chain &&
		for i in $(test_seq $SECOND_LAYER_SEQUENCE_START $SECOND_LAYER_SEQUENCE_END)
		do
			test_cummit $i &&
			but branch cummits/$i || return 1
		done &&
		but -c cummitGraph.generationVersion=1 cummit-graph write --reachable --split=no-merge &&
		test_line_count = 2 $graphdir/cummit-graph-chain &&
		test-tool read-graph >output &&
		cat >expect <<-EOF &&
		header: 43475048 1 $(test_oid oid_version) 4 1
		num_cummits: $NUM_SECOND_LAYER_CUMMITS
		chunks: oid_fanout oid_lookup cummit_metadata
		options:
		EOF
		test_cmp expect output &&
		but cummit-graph verify &&
		cat $graphdir/cummit-graph-chain
	)
'

# The new layer will be added without generation data chunk as it was not
# present on the layer underneath it.
#
#      7 cummits (No GDAT)
# ------------------------
#     16 cummits (No GDAT)
# ------------------------
#     64 cummits (GDAT)
#
test_expect_success 'do not write generation data chunk if not present on existing tip' '
	but clone mixed mixed-no-gdat &&
	(
		cd mixed-no-gdat &&
		for i in $(test_seq $THIRD_LAYER_SEQUENCE_START $THIRD_LAYER_SEQUENCE_END)
		do
			test_cummit $i &&
			but branch cummits/$i || return 1
		done &&
		but cummit-graph write --reachable --split=no-merge &&
		test_line_count = 3 $graphdir/cummit-graph-chain &&
		test-tool read-graph >output &&
		cat >expect <<-EOF &&
		header: 43475048 1 $(test_oid oid_version) 4 2
		num_cummits: $NUM_THIRD_LAYER_CUMMITS
		chunks: oid_fanout oid_lookup cummit_metadata
		options:
		EOF
		test_cmp expect output &&
		but cummit-graph verify
	)
'

# Number of cummits in each layer of the split-cummit graph before merge:
#
#      8 cummits (No GDAT)
# ------------------------
#      7 cummits (No GDAT)
# ------------------------
#     16 cummits (No GDAT)
# ------------------------
#     64 cummits (GDAT)
#
# The top two layers are merged and do not have generation data chunk as layer below them does
# not have generation data chunk.
#
#     15 cummits (No GDAT)
# ------------------------
#     16 cummits (No GDAT)
# ------------------------
#     64 cummits (GDAT)
#
test_expect_success 'do not write generation data chunk if the topmost remaining layer does not have generation data chunk' '
	but clone mixed-no-gdat mixed-merge-no-gdat &&
	(
		cd mixed-merge-no-gdat &&
		for i in $(test_seq $FOURTH_LAYER_SEQUENCE_START $FOURTH_LAYER_SEQUENCE_END)
		do
			test_cummit $i &&
			but branch cummits/$i || return 1
		done &&
		but cummit-graph write --reachable --split --size-multiple 1 &&
		test_line_count = 3 $graphdir/cummit-graph-chain &&
		test-tool read-graph >output &&
		cat >expect <<-EOF &&
		header: 43475048 1 $(test_oid oid_version) 4 2
		num_cummits: $(($NUM_THIRD_LAYER_CUMMITS + $NUM_FOURTH_LAYER_CUMMITS))
		chunks: oid_fanout oid_lookup cummit_metadata
		options:
		EOF
		test_cmp expect output &&
		but cummit-graph verify
	)
'

# Number of cummits in each layer of the split-cummit graph before merge:
#
#     16 cummits (No GDAT)
# ------------------------
#     15 cummits (No GDAT)
# ------------------------
#     16 cummits (No GDAT)
# ------------------------
#     64 cummits (GDAT)
#
# The top three layers are merged and has generation data chunk as the topmost remaining layer
# has generation data chunk.
#
#     47 cummits (GDAT)
# ------------------------
#     64 cummits (GDAT)
#
test_expect_success 'write generation data chunk if topmost remaining layer has generation data chunk' '
	but clone mixed-merge-no-gdat mixed-merge-gdat &&
	(
		cd mixed-merge-gdat &&
		for i in $(test_seq $FIFTH_LAYER_SEQUENCE_START $FIFTH_LAYER_SEQUENCE_END)
		do
			test_cummit $i &&
			but branch cummits/$i || return 1
		done &&
		but cummit-graph write --reachable --split --size-multiple 1 &&
		test_line_count = 2 $graphdir/cummit-graph-chain &&
		test-tool read-graph >output &&
		cat >expect <<-EOF &&
		header: 43475048 1 $(test_oid oid_version) 5 1
		num_cummits: $(($NUM_SECOND_LAYER_CUMMITS + $NUM_THIRD_LAYER_CUMMITS + $NUM_FOURTH_LAYER_CUMMITS + $NUM_FIFTH_LAYER_CUMMITS))
		chunks: oid_fanout oid_lookup cummit_metadata generation_data
		options: read_generation_data
		EOF
		test_cmp expect output
	)
'

test_expect_success 'write generation data chunk when cummit-graph chain is replaced' '
	but clone mixed mixed-replace &&
	(
		cd mixed-replace &&
		but cummit-graph write --reachable --split=replace &&
		test_path_is_file $graphdir/cummit-graph-chain &&
		test_line_count = 1 $graphdir/cummit-graph-chain &&
		verify_chain_files_exist $graphdir &&
		graph_read_expect $(($NUM_FIRST_LAYER_CUMMITS + $NUM_SECOND_LAYER_CUMMITS)) &&
		but cummit-graph verify
	)
'

test_done
