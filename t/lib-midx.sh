# test_midx_consistent <objdir>
test_midx_consistent () {
	ls $1/pack/pack-*.idx | xargs -n 1 basename | sort >expect &&
	test-tool read-midx $1 | grep ^pack-.*\.idx$ | sort >actual &&

	test_cmp expect actual &&
	git multi-pack-index --object-dir=$1 verify
}
