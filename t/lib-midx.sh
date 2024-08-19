# test_midx_consistent <objdir>
test_midx_consistent () {
	ls $1/pack/pack-*.idx | xargs -n 1 basename | sort >expect &&
	test-tool read-midx $1 | grep ^pack-.*\.idx$ | sort >actual &&

	test_cmp expect actual &&
	git multi-pack-index --object-dir=$1 verify
}

midx_checksum () {
	test-tool read-midx --checksum "$1"
}

midx_git_two_modes () {
	git -c core.multiPackIndex=false $1 >expect &&
	git -c core.multiPackIndex=true $1 >actual &&
	if [ "$2" = "sorted" ]
	then
		sort <expect >expect.sorted &&
		mv expect.sorted expect &&
		sort <actual >actual.sorted &&
		mv actual.sorted actual
	fi &&
	test_cmp expect actual
}

compare_results_with_midx () {
	MSG=$1
	test_expect_success "check normal git operations: $MSG" '
		midx_git_two_modes "rev-list --objects --all" &&
		midx_git_two_modes "log --raw" &&
		midx_git_two_modes "count-objects --verbose" &&
		midx_git_two_modes "cat-file --batch-all-objects --batch-check" &&
		midx_git_two_modes "cat-file --batch-all-objects --batch-check --unordered" sorted
	'
}
