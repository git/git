# Helper functions for testing bitmap performance; see p5310.

test_full_bitmap () {
	test_perf 'simulated clone' '
		but pack-objects --stdout --all </dev/null >/dev/null
	'

	test_perf 'simulated fetch' '
		have=$(but rev-list HEAD~100 -1) &&
		{
			echo HEAD &&
			echo ^$have
		} | but pack-objects --revs --stdout >/dev/null
	'

	test_perf 'pack to file (bitmap)' '
		but pack-objects --use-bitmap-index --all pack1b </dev/null >/dev/null
	'

	test_perf 'rev-list (cummits)' '
		but rev-list --all --use-bitmap-index >/dev/null
	'

	test_perf 'rev-list (objects)' '
		but rev-list --all --use-bitmap-index --objects >/dev/null
	'

	test_perf 'rev-list with tag negated via --not --all (objects)' '
		but rev-list perf-tag --not --all --use-bitmap-index --objects >/dev/null
	'

	test_perf 'rev-list with negative tag (objects)' '
		but rev-list HEAD --not perf-tag --use-bitmap-index --objects >/dev/null
	'

	test_perf 'rev-list count with blob:none' '
		but rev-list --use-bitmap-index --count --objects --all \
			--filter=blob:none >/dev/null
	'

	test_perf 'rev-list count with blob:limit=1k' '
		but rev-list --use-bitmap-index --count --objects --all \
			--filter=blob:limit=1k >/dev/null
	'

	test_perf 'rev-list count with tree:0' '
		but rev-list --use-bitmap-index --count --objects --all \
			--filter=tree:0 >/dev/null
	'

	test_perf 'simulated partial clone' '
		but pack-objects --stdout --all --filter=blob:none </dev/null >/dev/null
	'
}

test_partial_bitmap () {
	test_perf 'clone (partial bitmap)' '
		but pack-objects --stdout --all </dev/null >/dev/null
	'

	test_perf 'pack to file (partial bitmap)' '
		but pack-objects --use-bitmap-index --all pack2b </dev/null >/dev/null
	'

	test_perf 'rev-list with tree filter (partial bitmap)' '
		but rev-list --use-bitmap-index --count --objects --all \
			--filter=tree:0 >/dev/null
	'
}
