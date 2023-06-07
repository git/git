#!/bin/sh

test_description='exercise basic multi-pack bitmap functionality'
. ./test-lib.sh
. "${TEST_DIRECTORY}/lib-bitmap.sh"

# We'll be writing our own midx and bitmaps, so avoid getting confused by the
# automatic ones.
GIT_TEST_MULTI_PACK_INDEX=0
GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0

# This test exercise multi-pack bitmap functionality where the object order is
# stored and read from a special chunk within the MIDX, so use the default
# behavior here.
sane_unset GIT_TEST_MIDX_WRITE_REV
sane_unset GIT_TEST_MIDX_READ_RIDX

bitmap_reuse_tests() {
	from=$1
	to=$2
	writeLookupTable=false

	for i in $3-${$#}
	do
		case $i in
		"pack.writeBitmapLookupTable") writeLookupTable=true;;
		esac
	done

	test_expect_success "setup pack reuse tests ($from -> $to)" '
		rm -fr repo &&
		git init repo &&
		(
			cd repo &&
			git config pack.writeBitmapLookupTable '"$writeLookupTable"' &&
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
			git config pack.writeBitmapLookupTable '"$writeLookupTable"' &&
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
			git config pack.writeBitmapLookupTable '"$writeLookupTable"' &&
			git for-each-ref &&
			git rev-list --test-bitmap refs/tags/old-tip &&
			git rev-list --test-bitmap refs/tags/new-tip
		)
	'
}

test_midx_bitmap_cases () {
	writeLookupTable=false
	writeBitmapLookupTable=

	for i in "$@"
	do
		case $i in
		"pack.writeBitmapLookupTable")
			writeLookupTable=true
			writeBitmapLookupTable="$i"
			;;
		esac
	done

	test_expect_success 'setup test_repository' '
		rm -rf * .git &&
		git init &&
		git config pack.writeBitmapLookupTable '"$writeLookupTable"'
	'

	midx_bitmap_core

	bitmap_reuse_tests 'pack' 'MIDX' "$writeBitmapLookupTable"
	bitmap_reuse_tests 'MIDX' 'pack' "$writeBitmapLookupTable"
	bitmap_reuse_tests 'MIDX' 'MIDX' "$writeBitmapLookupTable"

	test_expect_success 'missing object closure fails gracefully' '
		rm -fr repo &&
		git init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&
			git config pack.writeBitmapLookupTable '"$writeLookupTable"' &&

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

	midx_bitmap_partial_tests

	test_expect_success 'removing a MIDX clears stale bitmaps' '
		rm -fr repo &&
		git init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&
			git config pack.writeBitmapLookupTable '"$writeLookupTable"' &&
			test_commit base &&
			git repack &&
			git multi-pack-index write --bitmap &&

			# Write a MIDX and bitmap; remove the MIDX but leave the bitmap.
			stale_bitmap=$midx-$(midx_checksum $objdir).bitmap &&
			rm $midx &&

			# Then write a new MIDX.
			test_commit new &&
			git repack &&
			git multi-pack-index write --bitmap &&

			test_path_is_file $midx &&
			test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&
			test_path_is_missing $stale_bitmap
		)
	'

	test_expect_success 'pack.preferBitmapTips' '
		git init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&
			git config pack.writeBitmapLookupTable '"$writeLookupTable"' &&

			test_commit_bulk --message="%s" 103 &&

			git log --format="%H" >commits.raw &&
			sort <commits.raw >commits &&

			git log --format="create refs/tags/%s %H" HEAD >refs &&
			git update-ref --stdin <refs &&

			git multi-pack-index write --bitmap &&
			test_path_is_file $midx &&
			test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&

			test-tool bitmap list-commits | sort >bitmaps &&
			comm -13 bitmaps commits >before &&
			test_line_count = 1 before &&

			perl -ne "printf(\"create refs/tags/include/%d \", $.); print" \
				<before | git update-ref --stdin &&

			rm -fr $midx-$(midx_checksum $objdir).bitmap &&
			rm -fr $midx &&

			git -c pack.preferBitmapTips=refs/tags/include \
				multi-pack-index write --bitmap &&
			test-tool bitmap list-commits | sort >bitmaps &&
			comm -13 bitmaps commits >after &&

			! test_cmp before after
		)
	'

	test_expect_success 'writing a bitmap with --refs-snapshot' '
		git init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&
			git config pack.writeBitmapLookupTable '"$writeLookupTable"' &&

			test_commit one &&
			test_commit two &&

			git rev-parse one >snapshot &&

			git repack -ad &&

			# First, write a MIDX which see both refs/tags/one and
			# refs/tags/two (causing both of those commits to receive
			# bitmaps).
			git multi-pack-index write --bitmap &&

			test_path_is_file $midx &&
			test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&

			test-tool bitmap list-commits | sort >bitmaps &&
			grep "$(git rev-parse one)" bitmaps &&
			grep "$(git rev-parse two)" bitmaps &&

			rm -fr $midx-$(midx_checksum $objdir).bitmap &&
			rm -fr $midx &&

			# Then again, but with a refs snapshot which only sees
			# refs/tags/one.
			git multi-pack-index write --bitmap --refs-snapshot=snapshot &&

			test_path_is_file $midx &&
			test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&

			test-tool bitmap list-commits | sort >bitmaps &&
			grep "$(git rev-parse one)" bitmaps &&
			! grep "$(git rev-parse two)" bitmaps
		)
	'

	test_expect_success 'write a bitmap with --refs-snapshot (preferred tips)' '
		git init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&
			git config pack.writeBitmapLookupTable '"$writeLookupTable"' &&

			test_commit_bulk --message="%s" 103 &&

			git log --format="%H" >commits.raw &&
			sort <commits.raw >commits &&

			git log --format="create refs/tags/%s %H" HEAD >refs &&
			git update-ref --stdin <refs &&

			git multi-pack-index write --bitmap &&
			test_path_is_file $midx &&
			test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&

			test-tool bitmap list-commits | sort >bitmaps &&
			comm -13 bitmaps commits >before &&
			test_line_count = 1 before &&

			(
				grep -vf before commits.raw &&
				# mark missing commits as preferred
				sed "s/^/+/" before
			) >snapshot &&

			rm -fr $midx-$(midx_checksum $objdir).bitmap &&
			rm -fr $midx &&

			git multi-pack-index write --bitmap --refs-snapshot=snapshot &&
			test-tool bitmap list-commits | sort >bitmaps &&
			comm -13 bitmaps commits >after &&

			! test_cmp before after
		)
	'

	test_expect_success 'hash-cache values are propagated from pack bitmaps' '
		rm -fr repo &&
		git init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&
			git config pack.writeBitmapLookupTable '"$writeLookupTable"' &&

			test_commit base &&
			test_commit base2 &&
			git repack -adb &&

			test-tool bitmap dump-hashes >pack.raw &&
			test_file_not_empty pack.raw &&
			sort pack.raw >pack.hashes &&

			test_commit new &&
			git repack &&
			git multi-pack-index write --bitmap &&

			test-tool bitmap dump-hashes >midx.raw &&
			sort midx.raw >midx.hashes &&

			# ensure that every namehash in the pack bitmap can be found in
			# the midx bitmap (i.e., that there are no oid-namehash pairs
			# unique to the pack bitmap).
			comm -23 pack.hashes midx.hashes >dropped.hashes &&
			test_must_be_empty dropped.hashes
		)
	'

	test_expect_success 'no .bitmap is written without any objects' '
		rm -fr repo &&
		git init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&
			git config pack.writeBitmapLookupTable '"$writeLookupTable"' &&

			empty="$(git pack-objects $objdir/pack/pack </dev/null)" &&
			cat >packs <<-EOF &&
			pack-$empty.idx
			EOF

			git multi-pack-index write --bitmap --stdin-packs \
				<packs 2>err &&

			grep "bitmap without any objects" err &&

			test_path_is_file $midx &&
			test_path_is_missing $midx-$(midx_checksum $objdir).bitmap
		)
	'

	test_expect_success 'graceful fallback when missing reverse index' '
		rm -fr repo &&
		git init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&
			git config pack.writeBitmapLookupTable '"$writeLookupTable"' &&

			test_commit base &&

			# write a pack and MIDX bitmap containing base
			git repack -adb &&
			git multi-pack-index write --bitmap &&

			GIT_TEST_MIDX_READ_RIDX=0 \
				git rev-list --use-bitmap-index HEAD 2>err &&
			! grep "ignoring extra bitmap file" err
		)
	'
}

test_midx_bitmap_cases

test_midx_bitmap_cases "pack.writeBitmapLookupTable"

test_expect_success 'multi-pack-index write writes lookup table if enabled' '
	rm -fr repo &&
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&
		test_commit base &&
		git config pack.writeBitmapLookupTable true &&
		git repack -ad &&
		GIT_TRACE2_EVENT="$(pwd)/trace" \
			git multi-pack-index write --bitmap &&
		grep "\"label\":\"writing_lookup_table\"" trace
	)
'

test_expect_success 'preferred pack change with existing MIDX bitmap' '
	git init preferred-pack-with-existing &&
	(
		cd preferred-pack-with-existing &&

		test_commit base &&
		test_commit other &&

		git rev-list --objects --no-object-names base >p1.objects &&
		git rev-list --objects --no-object-names other >p2.objects &&

		p1="$(git pack-objects "$objdir/pack/pack" \
			--delta-base-offset <p1.objects)" &&
		p2="$(git pack-objects "$objdir/pack/pack" \
			--delta-base-offset <p2.objects)" &&

		# Generate a MIDX containing the first two packs,
		# marking p1 as preferred, and ensure that it can be
		# successfully cloned.
		git multi-pack-index write --bitmap \
			--preferred-pack="pack-$p1.pack" &&
		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&
		git clone --no-local . clone1 &&

		# Then generate a new pack which sorts ahead of any
		# existing pack (by tweaking the pack prefix).
		test_commit foo &&
		git pack-objects --all --unpacked $objdir/pack/pack0 &&

		# Generate a new MIDX which changes the preferred pack
		# to a pack contained in the existing MIDX.
		git multi-pack-index write --bitmap \
			--preferred-pack="pack-$p2.pack" &&
		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&

		# When the above circumstances are met, the preferred
		# pack should change appropriately and clones should
		# (still) succeed.
		git clone --no-local . clone2
	)
'

test_expect_success 'tagged commits are selected for bitmapping' '
	rm -fr repo &&
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit --annotate base &&
		git repack -d &&

		# Remove refs/heads/main which points at the commit directly,
		# leaving only a reference to the annotated tag.
		git branch -M main &&
		git checkout base &&
		git branch -d main &&

		git multi-pack-index write --bitmap &&

		git rev-parse HEAD >want &&
		test-tool bitmap list-commits >actual &&
		grep $(cat want) actual
	)
'

corrupt_file () {
	chmod a+w "$1" &&
	printf "bogus" | dd of="$1" bs=1 seek="12" conv=notrunc
}

test_expect_success 'git fsck correctly identifies good and bad bitmaps' '
	git init valid &&
	test_when_finished rm -rf valid &&

	test_commit_bulk 20 &&
	git repack -adbf &&

	# Move pack-bitmap aside so it is not deleted
	# in next repack.
	packbitmap=$(ls .git/objects/pack/pack-*.bitmap) &&
	mv "$packbitmap" "$packbitmap.bak" &&

	test_commit_bulk 10 &&
	git repack -b --write-midx &&
	midxbitmap=$(ls .git/objects/pack/multi-pack-index-*.bitmap) &&

	# Copy MIDX bitmap to backup. Copy pack bitmap from backup.
	cp "$midxbitmap" "$midxbitmap.bak" &&
	cp "$packbitmap.bak" "$packbitmap" &&

	# fsck works at first
	git fsck 2>err &&
	test_must_be_empty err &&

	corrupt_file "$packbitmap" &&
	test_must_fail git fsck 2>err &&
	grep "bitmap file '\''$packbitmap'\'' has invalid checksum" err &&

	cp "$packbitmap.bak" "$packbitmap" &&
	corrupt_file "$midxbitmap" &&
	test_must_fail git fsck 2>err &&
	grep "bitmap file '\''$midxbitmap'\'' has invalid checksum" err &&

	corrupt_file "$packbitmap" &&
	test_must_fail git fsck 2>err &&
	grep "bitmap file '\''$midxbitmap'\'' has invalid checksum" err &&
	grep "bitmap file '\''$packbitmap'\'' has invalid checksum" err
'

test_expect_success 'corrupt MIDX with bitmap causes fallback' '
	git init corrupt-midx-bitmap &&
	(
		cd corrupt-midx-bitmap &&

		test_commit first &&
		git repack -d &&
		test_commit second &&
		git repack -d &&

		git multi-pack-index write --bitmap &&
		checksum=$(midx_checksum $objdir) &&
		for f in $midx $midx-$checksum.bitmap
		do
			mv $f $f.bak || return 1
		done &&

		# pack everything together, invalidating the MIDX
		git repack -ad &&
		# then restore the now-stale MIDX
		for f in $midx $midx-$checksum.bitmap
		do
			mv $f.bak $f || return 1
		done &&

		git rev-list --count --objects --use-bitmap-index HEAD >out 2>err &&
		# should attempt opening the broken pack twice (once
		# from the attempt to load it via the stale bitmap, and
		# again when attempting to load it from the stale MIDX)
		# before falling back to the non-MIDX case
		test 2 -eq $(grep -c "could not open pack" err) &&
		test 6 -eq $(cat out)
	)
'

test_done
