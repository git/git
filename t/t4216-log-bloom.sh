#!/bin/sh

test_description='git log for a path with Bloom filters'
. ./test-lib.sh

GIT_TEST_COMMIT_GRAPH=0
GIT_TEST_COMMIT_GRAPH_CHANGED_PATHS=0

test_expect_success 'setup test - repo, commits, commit graph, log outputs' '
	git init &&
	mkdir A A/B A/B/C &&
	test_commit c1 A/file1 &&
	test_commit c2 A/B/file2 &&
	test_commit c3 A/B/C/file3 &&
	test_commit c4 A/file1 &&
	test_commit c5 A/B/file2 &&
	test_commit c6 A/B/C/file3 &&
	test_commit c7 A/file1 &&
	test_commit c8 A/B/file2 &&
	test_commit c9 A/B/C/file3 &&
	test_commit c10 file_to_be_deleted &&
	git checkout -b side HEAD~4 &&
	test_commit side-1 file4 &&
	git checkout master &&
	git merge side &&
	test_commit c11 file5 &&
	mv file5 file5_renamed &&
	git add file5_renamed &&
	git commit -m "rename" &&
	rm file_to_be_deleted &&
	git add . &&
	git commit -m "file removed" &&
	git commit-graph write --reachable --changed-paths
'
graph_read_expect () {
	NUM_CHUNKS=5
	cat >expect <<- EOF
	header: 43475048 1 1 $NUM_CHUNKS 0
	num_commits: $1
	chunks: oid_fanout oid_lookup commit_metadata bloom_indexes bloom_data
	EOF
	test-tool read-graph >actual &&
	test_cmp expect actual
}

test_expect_success 'commit-graph write wrote out the bloom chunks' '
	graph_read_expect 15
'

# Turn off any inherited trace2 settings for this test.
sane_unset GIT_TRACE2 GIT_TRACE2_PERF GIT_TRACE2_EVENT
sane_unset GIT_TRACE2_PERF_BRIEF
sane_unset GIT_TRACE2_CONFIG_PARAMS

setup () {
	rm -f "$TRASH_DIRECTORY/trace.perf" &&
	git -c core.commitGraph=false log --pretty="format:%s" $1 >log_wo_bloom &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.perf" git -c core.commitGraph=true log --pretty="format:%s" $1 >log_w_bloom
}

test_bloom_filters_used () {
	log_args=$1
	bloom_trace_prefix="statistics:{\"filter_not_present\":${2:-0},\"maybe\""
	setup "$log_args" &&
	grep -q "$bloom_trace_prefix" "$TRASH_DIRECTORY/trace.perf" &&
	test_cmp log_wo_bloom log_w_bloom &&
    test_path_is_file "$TRASH_DIRECTORY/trace.perf"
}

test_bloom_filters_not_used () {
	log_args=$1
	setup "$log_args" &&
	! grep -q "statistics:{\"filter_not_present\":" "$TRASH_DIRECTORY/trace.perf" &&
	test_cmp log_wo_bloom log_w_bloom
}

for path in A A/B A/B/C A/file1 A/B/file2 A/B/C/file3 file4 file5 file5_renamed file_to_be_deleted
do
	for option in "" \
	      "--all" \
		      "--full-history" \
		      "--full-history --simplify-merges" \
		      "--simplify-merges" \
		      "--simplify-by-decoration" \
		      "--follow" \
		      "--first-parent" \
		      "--topo-order" \
		      "--date-order" \
		      "--author-date-order" \
		      "--ancestry-path side..master"
	do
		test_expect_success "git log option: $option for path: $path" '
			test_bloom_filters_used "$option -- $path" &&
			test_config commitgraph.readChangedPaths false &&
			test_bloom_filters_not_used "$option -- $path"
		'
	done
done

test_expect_success 'git log -- folder works with and without the trailing slash' '
	test_bloom_filters_used "-- A" &&
	test_bloom_filters_used "-- A/"
'

test_expect_success 'git log for path that does not exist. ' '
	test_bloom_filters_used "-- path_does_not_exist"
'

test_expect_success 'git log with --walk-reflogs does not use Bloom filters' '
	test_bloom_filters_not_used "--walk-reflogs -- A"
'

test_expect_success 'git log -- multiple path specs does not use Bloom filters' '
	test_bloom_filters_not_used "-- file4 A/file1"
'

test_expect_success 'git log -- "." pathspec at root does not use Bloom filters' '
	test_bloom_filters_not_used "-- ."
'

test_expect_success 'git log with wildcard that resolves to a single path uses Bloom filters' '
	test_bloom_filters_used "-- *4" &&
	test_bloom_filters_used "-- *renamed"
'

test_expect_success 'git log with wildcard that resolves to a multiple paths does not uses Bloom filters' '
	test_bloom_filters_not_used "-- *" &&
	test_bloom_filters_not_used "-- file*"
'

test_expect_success 'setup - add commit-graph to the chain without Bloom filters' '
	test_commit c14 A/anotherFile2 &&
	test_commit c15 A/B/anotherFile2 &&
	test_commit c16 A/B/C/anotherFile2 &&
	git commit-graph write --reachable --split --no-changed-paths &&
	test_line_count = 2 .git/objects/info/commit-graphs/commit-graph-chain
'

test_expect_success 'use Bloom filters even if the latest graph does not have Bloom filters' '
	# Ensure that the number of empty filters is equal to the number of
	# filters in the latest graph layer to prove that they are loaded (and
	# ignored).
	test_bloom_filters_used "-- A/B" 3
'

test_expect_success 'setup - add commit-graph to the chain with Bloom filters' '
	test_commit c17 A/anotherFile3 &&
	git commit-graph write --reachable --changed-paths --split &&
	test_line_count = 3 .git/objects/info/commit-graphs/commit-graph-chain
'

test_bloom_filters_used_when_some_filters_are_missing () {
	log_args=$1
	bloom_trace_prefix="statistics:{\"filter_not_present\":3,\"maybe\":6,\"definitely_not\":8"
	setup "$log_args" &&
	grep -q "$bloom_trace_prefix" "$TRASH_DIRECTORY/trace.perf" &&
	test_cmp log_wo_bloom log_w_bloom
}

test_expect_success 'Use Bloom filters if they exist in the latest but not all commit graphs in the chain.' '
	test_bloom_filters_used_when_some_filters_are_missing "-- A/B"
'

test_expect_success 'persist filter settings' '
	test_when_finished rm -rf .git/objects/info/commit-graph* &&
	rm -rf .git/objects/info/commit-graph* &&
	GIT_TRACE2_EVENT="$(pwd)/trace2.txt" \
		GIT_TRACE2_EVENT_NESTING=5 \
		GIT_TEST_BLOOM_SETTINGS_NUM_HASHES=9 \
		GIT_TEST_BLOOM_SETTINGS_BITS_PER_ENTRY=15 \
		git commit-graph write --reachable --changed-paths &&
	grep "{\"hash_version\":1,\"num_hashes\":9,\"bits_per_entry\":15" trace2.txt &&
	GIT_TRACE2_EVENT="$(pwd)/trace2-auto.txt" \
		GIT_TRACE2_EVENT_NESTING=5 \
		git commit-graph write --reachable --changed-paths &&
	grep "{\"hash_version\":1,\"num_hashes\":9,\"bits_per_entry\":15" trace2-auto.txt
'

test_expect_success 'correctly report changes over limit' '
	git init 513changes &&
	(
		cd 513changes &&
		for i in $(test_seq 1 513)
		do
			echo $i >file$i.txt || return 1
		done &&
		git add . &&
		git commit -m "files" &&
		git commit-graph write --reachable --changed-paths &&
		for i in $(test_seq 1 513)
		do
			git -c core.commitGraph=false log -- file$i.txt >expect &&
			git log -- file$i.txt >actual &&
			test_cmp expect actual || return 1
		done
	)
'

test_done
