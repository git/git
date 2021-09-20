#!/bin/sh

test_description='exercise basic multi-pack bitmap functionality'
. ./test-lib.sh
. "${TEST_DIRECTORY}/lib-bitmap.sh"

# We'll be writing our own midx and bitmaps, so avoid getting confused by the
# automatic ones.
GIT_TEST_MULTI_PACK_INDEX=0
GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0

objdir=.git/objects
midx=$objdir/pack/multi-pack-index

# midx_pack_source <obj>
midx_pack_source () {
	test-tool read-midx --show-objects .git/objects | grep "^$1 " | cut -f2
}

setup_bitmap_history

test_expect_success 'enable core.multiPackIndex' '
	git config core.multiPackIndex true
'

test_expect_success 'create single-pack midx with bitmaps' '
	git repack -ad &&
	git multi-pack-index write --bitmap &&
	test_path_is_file $midx &&
	test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&
	test_path_is_file $midx-$(midx_checksum $objdir).rev
'

basic_bitmap_tests

test_expect_success 'create new additional packs' '
	for i in $(test_seq 1 16)
	do
		test_commit "$i" &&
		git repack -d || return 1
	done &&

	git checkout -b other2 HEAD~8 &&
	for i in $(test_seq 1 8)
	do
		test_commit "side-$i" &&
		git repack -d || return 1
	done &&
	git checkout second
'

test_expect_success 'create multi-pack midx with bitmaps' '
	git multi-pack-index write --bitmap &&

	ls $objdir/pack/pack-*.pack >packs &&
	test_line_count = 25 packs &&

	test_path_is_file $midx &&
	test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&
	test_path_is_file $midx-$(midx_checksum $objdir).rev
'

basic_bitmap_tests

test_expect_success '--no-bitmap is respected when bitmaps exist' '
	git multi-pack-index write --bitmap &&

	test_commit respect--no-bitmap &&
	git repack -d &&

	test_path_is_file $midx &&
	test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&
	test_path_is_file $midx-$(midx_checksum $objdir).rev &&

	git multi-pack-index write --no-bitmap &&

	test_path_is_file $midx &&
	test_path_is_missing $midx-$(midx_checksum $objdir).bitmap &&
	test_path_is_missing $midx-$(midx_checksum $objdir).rev
'

test_expect_success 'setup midx with base from later pack' '
	# Write a and b so that "a" is a delta on top of base "b", since Git
	# prefers to delete contents out of a base rather than add to a shorter
	# object.
	test_seq 1 128 >a &&
	test_seq 1 130 >b &&

	git add a b &&
	git commit -m "initial commit" &&

	a=$(git rev-parse HEAD:a) &&
	b=$(git rev-parse HEAD:b) &&

	# In the first pack, "a" is stored as a delta to "b".
	p1=$(git pack-objects .git/objects/pack/pack <<-EOF
	$a
	$b
	EOF
	) &&

	# In the second pack, "a" is missing, and "b" is not a delta nor base to
	# any other object.
	p2=$(git pack-objects .git/objects/pack/pack <<-EOF
	$b
	$(git rev-parse HEAD)
	$(git rev-parse HEAD^{tree})
	EOF
	) &&

	git prune-packed &&
	# Use the second pack as the preferred source, so that "b" occurs
	# earlier in the MIDX object order, rendering "a" unusable for pack
	# reuse.
	git multi-pack-index write --bitmap --preferred-pack=pack-$p2.idx &&

	have_delta $a $b &&
	test $(midx_pack_source $a) != $(midx_pack_source $b)
'

rev_list_tests 'full bitmap with backwards delta'

test_expect_success 'clone with bitmaps enabled' '
	git clone --no-local --bare . clone-reverse-delta.git &&
	test_when_finished "rm -fr clone-reverse-delta.git" &&

	git rev-parse HEAD >expect &&
	git --git-dir=clone-reverse-delta.git rev-parse HEAD >actual &&
	test_cmp expect actual
'

bitmap_reuse_tests() {
	from=$1
	to=$2

	test_expect_success "setup pack reuse tests ($from -> $to)" '
		rm -fr repo &&
		git init repo &&
		(
			cd repo &&
			test_commit_bulk 16 &&
			git tag old-tip &&

			git config core.multiPackIndex true &&
			if test "MIDX" = "$from"
			then
				git repack -Ad &&
				git multi-pack-index write --bitmap
			else
				git repack -Adb
			fi
		)
	'

	test_expect_success "build bitmap from existing ($from -> $to)" '
		(
			cd repo &&
			test_commit_bulk --id=further 16 &&
			git tag new-tip &&

			if test "MIDX" = "$to"
			then
				git repack -d &&
				git multi-pack-index write --bitmap
			else
				git repack -Adb
			fi
		)
	'

	test_expect_success "verify resulting bitmaps ($from -> $to)" '
		(
			cd repo &&
			git for-each-ref &&
			git rev-list --test-bitmap refs/tags/old-tip &&
			git rev-list --test-bitmap refs/tags/new-tip
		)
	'
}

bitmap_reuse_tests 'pack' 'MIDX'
bitmap_reuse_tests 'MIDX' 'pack'
bitmap_reuse_tests 'MIDX' 'MIDX'

test_expect_success 'missing object closure fails gracefully' '
	rm -fr repo &&
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit loose &&
		test_commit packed &&

		# Do not pass "--revs"; we want a pack without the "loose"
		# commit.
		git pack-objects $objdir/pack/pack <<-EOF &&
		$(git rev-parse packed)
		EOF

		test_must_fail git multi-pack-index write --bitmap 2>err &&
		grep "doesn.t have full closure" err &&
		test_path_is_missing $midx
	)
'

test_expect_success 'setup partial bitmaps' '
	test_commit packed &&
	git repack &&
	test_commit loose &&
	git multi-pack-index write --bitmap 2>err &&
	test_path_is_file $midx &&
	test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&
	test_path_is_file $midx-$(midx_checksum $objdir).rev
'

basic_bitmap_tests HEAD~

test_expect_success 'removing a MIDX clears stale bitmaps' '
	rm -fr repo &&
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&
		test_commit base &&
		git repack &&
		git multi-pack-index write --bitmap &&

		# Write a MIDX and bitmap; remove the MIDX but leave the bitmap.
		stale_bitmap=$midx-$(midx_checksum $objdir).bitmap &&
		stale_rev=$midx-$(midx_checksum $objdir).rev &&
		rm $midx &&

		# Then write a new MIDX.
		test_commit new &&
		git repack &&
		git multi-pack-index write --bitmap &&

		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&
		test_path_is_file $midx-$(midx_checksum $objdir).rev &&
		test_path_is_missing $stale_bitmap &&
		test_path_is_missing $stale_rev
	)
'

test_expect_success 'pack.preferBitmapTips' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit_bulk --message="%s" 103 &&

		git log --format="%H" >commits.raw &&
		sort <commits.raw >commits &&

		git log --format="create refs/tags/%s %H" HEAD >refs &&
		git update-ref --stdin <refs &&

		git multi-pack-index write --bitmap &&
		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&
		test_path_is_file $midx-$(midx_checksum $objdir).rev &&

		test-tool bitmap list-commits | sort >bitmaps &&
		comm -13 bitmaps commits >before &&
		test_line_count = 1 before &&

		perl -ne "printf(\"create refs/tags/include/%d \", $.); print" \
			<before | git update-ref --stdin &&

		rm -fr $midx-$(midx_checksum $objdir).bitmap &&
		rm -fr $midx-$(midx_checksum $objdir).rev &&
		rm -fr $midx &&

		git -c pack.preferBitmapTips=refs/tags/include \
			multi-pack-index write --bitmap &&
		test-tool bitmap list-commits | sort >bitmaps &&
		comm -13 bitmaps commits >after &&

		! test_cmp before after
	)
'

test_done
