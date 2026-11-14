#!/bin/sh

test_description='git repack --write-midx=incremental'

. ./test-lib.sh

GIT_TEST_MULTI_PACK_INDEX=0
GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0
GIT_TEST_MULTI_PACK_INDEX_WRITE_INCREMENTAL=0

objdir=.git/objects
packdir=$objdir/pack
midxdir=$packdir/multi-pack-index.d
midx_chain=$midxdir/multi-pack-index-chain

# incrementally_repack N
#
# Make "N" new commits, each stored in their own pack, and then repacked
# with the --write-midx=incremental strategy.
incrementally_repack () {
	for i in $(test_seq 1 "$1")
	do
		test_commit "$i" &&

		git repack --geometric=2 -d --write-midx=incremental \
			--write-bitmap-index &&
		git multi-pack-index verify || return 1
	done
}

# Create packs with geometrically increasing sizes so that they
# satisfy the geometric progression and survive a --geometric=2
# repack without being rolled up. Creates 3 packs containing 1,
# 2, and 6 commits (3, 6, and 18 objects) respectively.
create_geometric_packs () {
	test_commit "small" &&
	git repack -d &&

	test_commit_bulk --message="medium" 2 &&
	test_commit_bulk --message="large" 6 &&

	git repack --geometric=2 -d --write-midx=incremental \
		--write-bitmap-index
}

# create_layer <test_commit_bulk args>
#
# Creates a new MIDX layer with the contents of "test_commit_bulk $@".
create_layer () {
	test_commit_bulk "$@" &&

	git multi-pack-index write --incremental --bitmap
}

# create_layers
#
# Reads lines of "<message> <nr>" from stdin and creates a new MIDX
# layer for each line. See create_layer above for more.
create_layers () {
	while read msg nr
	do
		create_layer --message="$msg" "$nr" || return 1
	done
}

test_expect_success '--write-midx=incremental without --geometric' '
	git init incremental-without-geometric &&
	(
		cd incremental-without-geometric &&

		git config maintenance.auto false &&

		test_commit first &&
		git repack -d &&

		test_commit second &&
		git repack --write-midx=incremental &&

		git multi-pack-index verify &&
		test_line_count = 1 $midx_chain &&
		cp $midx_chain $midx_chain.before &&

		# A second repack appends a new layer without
		# disturbing the existing one.
		test_commit third &&
		git repack --write-midx=incremental &&

		git multi-pack-index verify &&
		test_line_count = 2 $midx_chain &&
		head -n 1 $midx_chain.before >expect &&
		head -n 1 $midx_chain >actual &&
		test_cmp expect actual &&

		git fsck
	)
'

test_expect_success 'below layer threshold, tip packs excluded' '
	git init below-layer-threshold-tip-packs-excluded &&
	(
		cd below-layer-threshold-tip-packs-excluded &&

		git config maintenance.auto false &&
		git config repack.midxnewlayerthreshold 4 &&
		git config repack.midxsplitfactor 2 &&

		# Create 3 packs forming a geometric progression by
		# object count such that they are unmodified by the
		# initial repack. The MIDX chain thusly contains a
		# single layer with three packs.
		create_geometric_packs &&
		ls $packdir/pack-*.idx | sort >packs.before &&
		test_line_count = 1 $midx_chain &&
		cp $midx_chain $midx_chain.before &&

		# Repack a new commit. Since the layer threshold is
		# unmet, a new MIDX layer is added on top of the
		# existing one.
		test_commit extra &&
		git repack --geometric=2 -d --write-midx=incremental \
			--write-bitmap-index &&
		git multi-pack-index verify &&

		ls $packdir/pack-*.idx | sort >packs.after &&
		comm -13 packs.before packs.after >packs.new &&
		test_line_count = 1 packs.new &&

		test_line_count = 2 "$midx_chain" &&
		head -n 1 "$midx_chain.before" >expect &&
		head -n 1 "$midx_chain" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'above layer threshold, tip packs repacked' '
	git init above-layer-threshold-tip-packs-repacked &&
	(
		cd above-layer-threshold-tip-packs-repacked &&

		git config maintenance.auto false &&
		git config repack.midxnewlayerthreshold 2 &&
		git config repack.midxsplitfactor 2 &&

		# Same setup, but with the layer threshold set to 2.
		# Since the tip MIDX layer meets that threshold, its
		# packs are considered repack candidates.
		create_geometric_packs &&
		cp $midx_chain $midx_chain.before &&

		# Perturb the existing progression such that it is
		# rolled up into a single new pack, invalidating the
		# existing MIDX layer and replacing it with a new one.
		test_commit extra &&
		git repack -d &&
		git repack --geometric=2 -d --write-midx=incremental \
			--write-bitmap-index &&

		! test_cmp $midx_chain.before $midx_chain &&
		test_line_count = 1 $midx_chain &&

		git multi-pack-index verify
	)
'

test_expect_success 'above layer threshold, tip layer preserved' '
	git init above-layer-threshold-tip-layer-preserved &&
	(
		cd above-layer-threshold-tip-layer-preserved &&

		git config maintenance.auto false &&
		git config repack.midxnewlayerthreshold 2 &&
		git config repack.midxsplitfactor 2 &&

		test_commit_bulk --message="medium" 2 &&
		test_commit_bulk --message="large" 6 &&

		git repack --geometric=2 -d --write-midx=incremental \
			--write-bitmap-index &&

		test_line_count = 1 "$midx_chain" &&
		ls $packdir/pack-*.idx | sort >packs.before &&
		cp $midx_chain $midx_chain.before &&

		# Create objects to form a pack satisfying the geometric
		# progression (thus preserving the tip layer), but not
		# so large that it meets the layer merging condition.
		test_commit_bulk --message="small" 1 &&
		git repack --geometric=2 -d --write-midx=incremental \
			--write-bitmap-index &&

		ls $packdir/pack-*.idx | sort >packs.after &&
		comm -13 packs.before packs.after >packs.new &&

		test_line_count = 1 packs.new &&
		test_line_count = 3 packs.after &&
		test_line_count = 2 "$midx_chain" &&
		head -n 1 "$midx_chain.before" >expect &&
		head -n 1 "$midx_chain" >actual &&
		test_cmp expect actual &&

		git multi-pack-index verify
	)
'

test_expect_success 'above layer threshold, tip packs preserved' '
	git init above-layer-threshold-tip-packs-preserved &&
	(
		cd above-layer-threshold-tip-packs-preserved &&

		git config maintenance.auto false &&
		git config repack.midxnewlayerthreshold 2 &&
		git config repack.midxsplitfactor 2 &&

		create_geometric_packs &&
		ls $packdir/pack-*.idx | sort >packs.before &&
		cp $midx_chain $midx_chain.before &&

		# Same setup as above, but this time the new objects do
		# not satisfy the new layer merging condition, resulting
		# in a new tip layer.
		test_commit_bulk --message="huge" 18 &&
		git repack --geometric=2 -d --write-midx=incremental \
			--write-bitmap-index &&

		ls $packdir/pack-*.idx | sort >packs.after &&
		comm -13 packs.before packs.after >packs.new &&

		! test_cmp $midx_chain.before $midx_chain &&
		test_line_count = 1 $midx_chain &&
		test_line_count = 1 packs.new &&

		git multi-pack-index verify
	)
'

test_expect_success 'new tip absorbs multiple layers' '
	git init new-tip-absorbs-multiple-layers &&
	(
		cd new-tip-absorbs-multiple-layers &&

		git config maintenance.auto false &&
		git config repack.midxnewlayerthreshold 1 &&
		git config repack.midxsplitfactor 2 &&

		# Build a 4-layer chain where each layer is too small to
		# absorb the one below it. The sizes must satisfy L(n) <
		# L(n-1)/2 for each adjacent pair:
		#
		#   L0 (oldest): 75 obj (25 commits)
		#   L1:          21 obj  (7 commits, 21 < 75/2)
		#   L2:           9 obj  (3 commits,  9 < 21/2)
		#   L3 (tip):     3 obj  (1 commit,   3 <  9/2)
		create_layers <<-\EOF &&
		L0 25
		L1 7
		L2 3
		L3 1
		EOF

		test_line_count = 4 "$midx_chain" &&
		cp $midx_chain $midx_chain.before &&

		# Now add a new commit. The merging condition is
		# satisfied between L3-L1, but violated at L0, which is
		# too large relative to the accumulated size.
		#
		# As a result, the chain shrinks from 4 to 2 layers.
		test_commit new &&
		git repack --geometric=2 -d --write-midx=incremental \
			--write-bitmap-index &&

		! test_cmp $midx_chain.before $midx_chain &&
		test_line_count = 2 "$midx_chain" &&
		git multi-pack-index verify
	)
'

test_expect_success 'compaction of older layers' '
	git init compaction-of-older-layers &&
	(
		cd compaction-of-older-layers &&

		git config maintenance.auto false &&
		git config repack.midxnewlayerthreshold 1 &&
		git config repack.midxsplitfactor 2 &&

		# Build a chain with two small layers at the bottom
		# and a larger barrier layer on top, producing a
		# chain that violates the compaction invariant, since
		# the two small layers would normally have been merged.
		create_layers <<-\EOF &&
		one 2
		two 4
		barrier 54
		EOF

		cp $midx_chain $midx_chain.before &&

		# Running an incremental repack compacts the two
		# small layers at the bottom of the chain as a
		# separate step in the compaction plan.
		test_commit another &&
		git repack --geometric=2 -d --write-midx=incremental \
			--write-bitmap-index &&

		test_line_count = 2 "$midx_chain" &&
		git multi-pack-index verify
	)
'

test_expect_success 'geometric rollup with surviving tip packs' '
	git init geometric-rollup-with-surviving-tip-packs &&
	(
		cd geometric-rollup-with-surviving-tip-packs &&

		git config maintenance.auto false &&
		git config repack.midxnewlayerthreshold 1 &&
		git config repack.midxsplitfactor 2 &&

		# Create a pack large enough to anchor the geometric
		# progression when small packs are added alongside it.
		create_layer --message="big" 5 &&

		test_line_count = 1 "$midx_chain" &&
		cp $midx_chain $midx_chain.before &&

		# Repack a small number of objects such that the
		# progression is unbothered. Note that the existing pack
		# is considered a repack candidate as the new layer
		# threshold is set to 1.
		test_commit small-1 &&
		git repack -d &&
		git repack --geometric=2 -d --write-midx=incremental \
			--write-bitmap-index &&

		! test_cmp $midx_chain.before $midx_chain &&
		cp $midx_chain $midx_chain.before
	)
'

test_expect_success 'kept packs are excluded from repack' '
	git init kept-packs-excluded-from-repack &&
	(
		cd kept-packs-excluded-from-repack &&

		git config maintenance.auto false &&
		git config repack.midxnewlayerthreshold 1 &&
		git config repack.midxsplitfactor 2 &&

		# Create two equal-sized packs, marking one as kept.
		for i in A B
		do
			test_commit "$i" && git repack -d || return 1
		done &&

		keep=$(ls $packdir/pack-*.idx | head -n 1) &&
		touch "${keep%.idx}.keep" &&

		# The kept pack is excluded as a repacking candidate
		# entirely, so no rollup occurs as there is only one
		# non-kept pack. A new MIDX layer is written containing
		# that pack.
		git repack --geometric=2 -d --write-midx=incremental &&

		test-tool read-midx $objdir >actual &&
		grep "^pack-.*\.idx$" actual >actual.packs &&
		test_line_count = 1 actual.packs &&
		test_grep ! "$keep" actual.packs &&

		git multi-pack-index verify &&

		# All objects (from both kept and non-kept packs)
		# must still be accessible.
		git fsck
	)
'

test_expect_success 'incremental MIDX with --max-pack-size' '
	git init incremental-midx-with--max-pack-size &&
	(
		cd incremental-midx-with--max-pack-size &&

		git config maintenance.auto false &&
		git config repack.midxnewlayerthreshold 1 &&
		git config repack.midxsplitfactor 2 &&

		create_layer --message="base" 1 &&

		# Now add enough data that a small --max-pack-size will
		# cause pack-objects to split its output. Create objects
		# large enough to fill multiple packs.
		test-tool genrandom foo 1M >big1 &&
		test-tool genrandom bar 1M >big2 &&
		git add big1 big2 &&
		test_tick &&
		git commit -a -m "big blobs" &&
		git repack -d &&

		git repack --geometric=2 -d --write-midx=incremental \
			--write-bitmap-index --max-pack-size=1M &&

		test_line_count = 1 "$midx_chain" &&
		test-tool read-midx $objdir >actual &&
		grep "^pack-.*\.idx$" actual >actual.packs &&
		test_line_count -gt 1 actual.packs &&

		git multi-pack-index verify
	)
'

test_expect_success 'noop repack preserves valid MIDX chain' '
	git init noop-repack-preserves-valid-midx-chain &&
	(
		cd noop-repack-preserves-valid-midx-chain &&

		git config maintenance.auto false &&
		git config repack.midxnewlayerthreshold 1 &&
		git config repack.midxsplitfactor 2 &&

		create_layer --message="base" 1 &&

		git multi-pack-index verify &&
		cp $midx_chain $midx_chain.before &&

		# Running again with no new objects should not break
		# the MIDX chain. It produces "Nothing new to pack."
		git repack --geometric=2 -d --write-midx=incremental \
			--write-bitmap-index &&

		test_cmp $midx_chain.before $midx_chain &&

		git multi-pack-index verify &&
		git fsck
	)
'

test_expect_success 'repack -ad removes stale incremental chain' '
	git init repack--ad-removes-stale-incremental-chain &&
	(
		cd repack--ad-removes-stale-incremental-chain &&

		git config maintenance.auto false &&
		git config repack.midxnewlayerthreshold 1 &&
		git config repack.midxsplitfactor 2 &&

		create_layers <<-\EOF &&
		one 1
		two 1
		EOF

		test_path_is_file $midx_chain &&
		test_line_count = 2 $midx_chain &&

		git repack -ad &&

		test_path_is_missing $packdir/multi-pack-index &&
		test_dir_is_empty $midxdir
	)
'

test_expect_success 'repack -ad --write-midx=incremental is safe' '
	git init ad-incremental-midx &&
	(
		cd ad-incremental-midx &&

		git config maintenance.auto false &&

		# Build a MIDX chain with multiple layers referencing
		# distinct packs.
		test_commit first &&
		git repack -d &&

		test_commit second &&
		git repack -d --write-midx=incremental &&

		git multi-pack-index verify &&
		test_line_count = 1 $midx_chain &&

		# Now do a full -ad repack. The new pack contains all
		# objects, but any retained MIDX layers still reference
		# the now-deleted packs.
		test_commit third &&
		git repack -ad --write-midx=incremental &&

		git multi-pack-index verify &&
		git fsck &&
		git rev-list --all --objects >/dev/null
	)
'

test_expect_success 'repack rejects invalid midxSplitFactor' '
	test_when_finished "rm -fr bad-split-factor" &&
	git init bad-split-factor &&
	(
		cd bad-split-factor &&
		test_commit base &&

		for v in 0 1 -1
		do
			test_must_fail git -c repack.midxSplitFactor=$v \
				repack -d --geometric=2 --write-midx=incremental 2>err &&
			test_grep "invalid value for --midx-split-factor" err ||
			return 1
		done
	)
'

test_expect_success 'repack rejects invalid midxNewLayerThreshold' '
	test_when_finished "rm -fr bad-layer-threshold" &&
	git init bad-layer-threshold &&
	(
		cd bad-layer-threshold &&
		test_commit base &&

		for v in 0 -1
		do
			test_must_fail git -c repack.midxNewLayerThreshold=$v \
				repack -d --geometric=2 --write-midx=incremental 2>err &&
			test_grep "invalid value for --midx-new-layer-threshold" err ||
			return 1
		done
	)
'

test_done
