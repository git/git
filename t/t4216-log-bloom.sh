#!/bin/sh

test_description='git log for a path with Bloom filters'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

GIT_TEST_cummit_GRAPH=0
GIT_TEST_cummit_GRAPH_CHANGED_PATHS=0

test_expect_success 'setup test - repo, cummits, cummit graph, log outputs' '
	git init &&
	mkdir A A/B A/B/C &&
	test_cummit c1 A/file1 &&
	test_cummit c2 A/B/file2 &&
	test_cummit c3 A/B/C/file3 &&
	test_cummit c4 A/file1 &&
	test_cummit c5 A/B/file2 &&
	test_cummit c6 A/B/C/file3 &&
	test_cummit c7 A/file1 &&
	test_cummit c8 A/B/file2 &&
	test_cummit c9 A/B/C/file3 &&
	test_cummit c10 file_to_be_deleted &&
	git checkout -b side HEAD~4 &&
	test_cummit side-1 file4 &&
	git checkout main &&
	git merge side &&
	test_cummit c11 file5 &&
	mv file5 file5_renamed &&
	git add file5_renamed &&
	git cummit -m "rename" &&
	rm file_to_be_deleted &&
	git add . &&
	git cummit -m "file removed" &&
	git cummit --allow-empty -m "empty" &&
	git cummit-graph write --reachable --changed-paths &&

	test_oid_cache <<-EOF
	oid_version sha1:1
	oid_version sha256:2
	EOF
'

graph_read_expect () {
	NUM_CHUNKS=6
	cat >expect <<- EOF
	header: 43475048 1 $(test_oid oid_version) $NUM_CHUNKS 0
	num_cummits: $1
	chunks: oid_fanout oid_lookup cummit_metadata generation_data bloom_indexes bloom_data
	options: bloom(1,10,7) read_generation_data
	EOF
	test-tool read-graph >actual &&
	test_cmp expect actual
}

test_expect_success 'cummit-graph write wrote out the bloom chunks' '
	graph_read_expect 16
'

# Turn off any inherited trace2 settings for this test.
sane_unset GIT_TRACE2 GIT_TRACE2_PERF GIT_TRACE2_EVENT
sane_unset GIT_TRACE2_PERF_BRIEF
sane_unset GIT_TRACE2_CONFIG_PARAMS

setup () {
	rm -f "$TRASH_DIRECTORY/trace.perf" &&
	git -c core.cummitGraph=false log --pretty="format:%s" $1 >log_wo_bloom &&
	GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.perf" git -c core.cummitGraph=true log --pretty="format:%s" $1 >log_w_bloom
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
		      "--ancestry-path side..main"
	do
		test_expect_success "git log option: $option for path: $path" '
			test_bloom_filters_used "$option -- $path" &&
			test_config cummitgraph.readChangedPaths false &&
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

test_expect_success 'setup - add cummit-graph to the chain without Bloom filters' '
	test_cummit c14 A/anotherFile2 &&
	test_cummit c15 A/B/anotherFile2 &&
	test_cummit c16 A/B/C/anotherFile2 &&
	git cummit-graph write --reachable --split --no-changed-paths &&
	test_line_count = 2 .git/objects/info/cummit-graphs/cummit-graph-chain
'

test_expect_success 'use Bloom filters even if the latest graph does not have Bloom filters' '
	# Ensure that the number of empty filters is equal to the number of
	# filters in the latest graph layer to prove that they are loaded (and
	# ignored).
	test_bloom_filters_used "-- A/B" 3
'

test_expect_success 'setup - add cummit-graph to the chain with Bloom filters' '
	test_cummit c17 A/anotherFile3 &&
	git cummit-graph write --reachable --changed-paths --split &&
	test_line_count = 3 .git/objects/info/cummit-graphs/cummit-graph-chain
'

test_bloom_filters_used_when_some_filters_are_missing () {
	log_args=$1
	bloom_trace_prefix="statistics:{\"filter_not_present\":3,\"maybe\":6,\"definitely_not\":9"
	setup "$log_args" &&
	grep -q "$bloom_trace_prefix" "$TRASH_DIRECTORY/trace.perf" &&
	test_cmp log_wo_bloom log_w_bloom
}

test_expect_success 'Use Bloom filters if they exist in the latest but not all cummit graphs in the chain.' '
	test_bloom_filters_used_when_some_filters_are_missing "-- A/B"
'

test_expect_success 'persist filter settings' '
	test_when_finished rm -rf .git/objects/info/cummit-graph* &&
	rm -rf .git/objects/info/cummit-graph* &&
	GIT_TRACE2_EVENT="$(pwd)/trace2.txt" \
		GIT_TEST_BLOOM_SETTINGS_NUM_HASHES=9 \
		GIT_TEST_BLOOM_SETTINGS_BITS_PER_ENTRY=15 \
		git cummit-graph write --reachable --changed-paths &&
	grep "{\"hash_version\":1,\"num_hashes\":9,\"bits_per_entry\":15,\"max_changed_paths\":512" trace2.txt &&
	GIT_TRACE2_EVENT="$(pwd)/trace2-auto.txt" \
		git cummit-graph write --reachable --changed-paths &&
	grep "{\"hash_version\":1,\"num_hashes\":9,\"bits_per_entry\":15,\"max_changed_paths\":512" trace2-auto.txt
'

test_max_changed_paths () {
	grep "\"max_changed_paths\":$1" $2
}

test_filter_not_computed () {
	grep "\"key\":\"filter-not-computed\",\"value\":\"$1\"" $2
}

test_filter_computed () {
	grep "\"key\":\"filter-computed\",\"value\":\"$1\"" $2
}

test_filter_trunc_empty () {
	grep "\"key\":\"filter-trunc-empty\",\"value\":\"$1\"" $2
}

test_filter_trunc_large () {
	grep "\"key\":\"filter-trunc-large\",\"value\":\"$1\"" $2
}

test_expect_success 'correctly report changes over limit' '
	git init limits &&
	(
		cd limits &&
		mkdir d &&
		mkdir d/e &&

		for i in $(test_seq 1 2)
		do
			printf $i >d/file$i.txt &&
			printf $i >d/e/file$i.txt || return 1
		done &&

		mkdir mode &&
		printf bash >mode/script.sh &&

		mkdir foo &&
		touch foo/bar &&
		touch foo.txt &&

		git add d foo foo.txt mode &&
		git cummit -m "files" &&

		# Commit has 7 file and 4 directory adds
		GIT_TEST_BLOOM_SETTINGS_MAX_CHANGED_PATHS=10 \
			GIT_TRACE2_EVENT="$(pwd)/trace" \
			git cummit-graph write --reachable --changed-paths &&
		test_max_changed_paths 10 trace &&
		test_filter_computed 1 trace &&
		test_filter_trunc_large 1 trace &&

		for path in $(git ls-tree -r --name-only HEAD)
		do
			git -c cummitGraph.readChangedPaths=false log \
				-- $path >expect &&
			git log -- $path >actual &&
			test_cmp expect actual || return 1
		done &&

		# Make a variety of path changes
		printf new1 >d/e/file1.txt &&
		printf new2 >d/file2.txt &&
		rm d/e/file2.txt &&
		rm -r foo &&
		printf text >foo &&
		mkdir f &&
		printf new1 >f/file1.txt &&

		# including a mode-only change (counts as modified)
		git update-index --chmod=+x mode/script.sh &&

		git add foo d f &&
		git cummit -m "complicated" &&

		# start from scratch and rebuild
		rm -f .git/objects/info/cummit-graph &&
		GIT_TEST_BLOOM_SETTINGS_MAX_CHANGED_PATHS=10 \
			GIT_TRACE2_EVENT="$(pwd)/trace-edit" \
			git cummit-graph write --reachable --changed-paths &&
		test_max_changed_paths 10 trace-edit &&
		test_filter_computed 2 trace-edit &&
		test_filter_trunc_large 2 trace-edit &&

		for path in $(git ls-tree -r --name-only HEAD)
		do
			git -c cummitGraph.readChangedPaths=false log \
				-- $path >expect &&
			git log -- $path >actual &&
			test_cmp expect actual || return 1
		done &&

		# start from scratch and rebuild
		rm -f .git/objects/info/cummit-graph &&
		GIT_TEST_BLOOM_SETTINGS_MAX_CHANGED_PATHS=11 \
			GIT_TRACE2_EVENT="$(pwd)/trace-update" \
			git cummit-graph write --reachable --changed-paths &&
		test_max_changed_paths 11 trace-update &&
		test_filter_computed 2 trace-update &&
		test_filter_trunc_large 0 trace-update &&

		for path in $(git ls-tree -r --name-only HEAD)
		do
			git -c cummitGraph.readChangedPaths=false log \
				-- $path >expect &&
			git log -- $path >actual &&
			test_cmp expect actual || return 1
		done
	)
'

test_expect_success 'correctly report cummits with no changed paths' '
	git init empty &&
	test_when_finished "rm -fr empty" &&
	(
		cd empty &&

		git cummit --allow-empty -m "initial cummit" &&

		GIT_TRACE2_EVENT="$(pwd)/trace.event" \
			git cummit-graph write --reachable --changed-paths &&
		test_filter_computed 1 trace.event &&
		test_filter_not_computed 0 trace.event &&
		test_filter_trunc_empty 1 trace.event &&
		test_filter_trunc_large 0 trace.event
	)
'

test_expect_success 'Bloom generation is limited by --max-new-filters' '
	(
		cd limits &&
		test_cummit c2 filter &&
		test_cummit c3 filter &&
		test_cummit c4 no-filter &&

		rm -f trace.event &&
		GIT_TRACE2_EVENT="$(pwd)/trace.event" \
			git cummit-graph write --reachable --split=replace \
				--changed-paths --max-new-filters=2 &&

		test_filter_computed 2 trace.event &&
		test_filter_not_computed 3 trace.event &&
		test_filter_trunc_empty 0 trace.event &&
		test_filter_trunc_large 0 trace.event
	)
'

test_expect_success 'Bloom generation backfills previously-skipped filters' '
	# Check specifying cummitGraph.maxNewFilters over "git config" works.
	test_config -C limits cummitGraph.maxNewFilters 1 &&
	(
		cd limits &&

		rm -f trace.event &&
		GIT_TRACE2_EVENT="$(pwd)/trace.event" \
			git cummit-graph write --reachable --changed-paths \
				--split=replace &&
		test_filter_computed 1 trace.event &&
		test_filter_not_computed 4 trace.event &&
		test_filter_trunc_empty 0 trace.event &&
		test_filter_trunc_large 0 trace.event
	)
'

test_expect_success '--max-new-filters overrides configuration' '
	git init override &&
	test_when_finished "rm -fr override" &&
	test_config -C override cummitGraph.maxNewFilters 2 &&
	(
		cd override &&
		test_cummit one &&
		test_cummit two &&

		rm -f trace.event &&
		GIT_TRACE2_EVENT="$(pwd)/trace.event" \
			git cummit-graph write --reachable --changed-paths \
				--max-new-filters=1 &&
		test_filter_computed 1 trace.event &&
		test_filter_not_computed 1 trace.event &&
		test_filter_trunc_empty 0 trace.event &&
		test_filter_trunc_large 0 trace.event
	)
'

test_expect_success 'Bloom generation backfills empty cummits' '
	git init empty &&
	test_when_finished "rm -fr empty" &&
	(
		cd empty &&
		for i in $(test_seq 1 6)
		do
			git cummit --allow-empty -m "$i" || return 1
		done &&

		# Generate Bloom filters for empty cummits 1-6, two at a time.
		for i in $(test_seq 1 3)
		do
			rm -f trace.event &&
			GIT_TRACE2_EVENT="$(pwd)/trace.event" \
				git cummit-graph write --reachable \
					--changed-paths --max-new-filters=2 &&
			test_filter_computed 2 trace.event &&
			test_filter_not_computed 4 trace.event &&
			test_filter_trunc_empty 2 trace.event &&
			test_filter_trunc_large 0 trace.event || return 1
		done &&

		# Finally, make sure that once all cummits have filters, that
		# none are subsequently recomputed.
		rm -f trace.event &&
		GIT_TRACE2_EVENT="$(pwd)/trace.event" \
			git cummit-graph write --reachable \
				--changed-paths --max-new-filters=2 &&
		test_filter_computed 0 trace.event &&
		test_filter_not_computed 6 trace.event &&
		test_filter_trunc_empty 0 trace.event &&
		test_filter_trunc_large 0 trace.event
	)
'

test_done
