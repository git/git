# Helper functions for testing bitmap performance; see p5310.

test_full_bitmap () {
	test_perf 'simulated clone' '
		git pack-objects --stdout --all </dev/null >/dev/null
	'

	test_perf 'simulated fetch' '
		have=$(git rev-list HEAD~100 -1) &&
		{
			echo HEAD &&
			echo ^$have
		} | git pack-objects --revs --stdout >/dev/null
	'

	test_perf 'pack to file (bitmap)' '
		git pack-objects --use-bitmap-index --all pack1b </dev/null >/dev/null
	'

	test_perf 'rev-list (commits)' '
		git rev-list --all --use-bitmap-index >/dev/null
	'

	test_perf 'rev-list (objects)' '
		git rev-list --all --use-bitmap-index --objects >/dev/null
	'

	test_perf 'rev-list with tag negated via --not --all (objects)' '
		git rev-list perf-tag --not --all --use-bitmap-index --objects >/dev/null
	'

	test_perf 'rev-list with negative tag (objects)' '
		git rev-list HEAD --not perf-tag --use-bitmap-index --objects >/dev/null
	'

	test_perf 'rev-list count with blob:none' '
		git rev-list --use-bitmap-index --count --objects --all \
			--filter=blob:none >/dev/null
	'

	test_perf 'rev-list count with blob:limit=1k' '
		git rev-list --use-bitmap-index --count --objects --all \
			--filter=blob:limit=1k >/dev/null
	'

	test_perf 'rev-list count with tree:0' '
		git rev-list --use-bitmap-index --count --objects --all \
			--filter=tree:0 >/dev/null
	'

	test_perf 'simulated partial clone' '
		git pack-objects --stdout --all --filter=blob:none </dev/null >/dev/null
	'
}

test_partial_bitmap () {
	test_perf 'clone (partial bitmap)' '
		git pack-objects --stdout --all </dev/null >/dev/null
	'

	test_perf 'pack to file (partial bitmap)' '
		git pack-objects --use-bitmap-index --all pack2b </dev/null >/dev/null
	'

	test_perf 'rev-list with tree filter (partial bitmap)' '
		git rev-list --use-bitmap-index --count --objects --all \
			--filter=tree:0 >/dev/null
	'
}
