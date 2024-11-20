#!/bin/sh

test_description='rev-list with .keep packs'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit loose &&
	test_commit packed &&
	test_commit kept &&

	KEPT_PACK=$(git pack-objects --revs .git/objects/pack/pack <<-EOF
	refs/tags/kept
	^refs/tags/packed
	EOF
	) &&
	MISC_PACK=$(git pack-objects --revs .git/objects/pack/pack <<-EOF
	refs/tags/packed
	^refs/tags/loose
	EOF
	) &&

	touch .git/objects/pack/pack-$KEPT_PACK.keep
'

rev_list_objects () {
	git rev-list "$@" >out &&
	sort out
}

idx_objects () {
	git show-index <$1 >expect-idx &&
	cut -d" " -f2 <expect-idx | sort
}

test_expect_success '--no-kept-objects excludes trees and blobs in .keep packs' '
	rev_list_objects --objects --all --no-object-names >kept &&
	rev_list_objects --objects --all --no-object-names --no-kept-objects >no-kept &&

	idx_objects .git/objects/pack/pack-$KEPT_PACK.idx >expect &&
	comm -3 kept no-kept >actual &&

	test_cmp expect actual
'

test_expect_success '--no-kept-objects excludes kept non-MIDX object' '
	test_config core.multiPackIndex true &&

	# Create a pack with just the commit object in pack, and do not mark it
	# as kept (even though it appears in $KEPT_PACK, which does have a .keep
	# file).
	MIDX_PACK=$(git pack-objects .git/objects/pack/pack <<-EOF
	$(git rev-parse kept)
	EOF
	) &&

	# Write a MIDX containing all packs, but use the version of the commit
	# at "kept" in a non-kept pack by touching $MIDX_PACK.
	touch .git/objects/pack/pack-$MIDX_PACK.pack &&
	git multi-pack-index write &&

	rev_list_objects --objects --no-object-names --no-kept-objects HEAD >actual &&
	(
		idx_objects .git/objects/pack/pack-$MISC_PACK.idx &&
		git rev-list --objects --no-object-names refs/tags/loose
	) | sort >expect &&
	test_cmp expect actual
'

test_done
