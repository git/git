#!/bin/sh

test_description='exercise basic multi-pack bitmap functionality'
. ./test-lib.sh
. "${TEST_DIRECTORY}/lib-bitmap.sh"

# We'll be writing our own midx and bitmaps, so avoid getting confused by the
# automatic ones.
BUT_TEST_MULTI_PACK_INDEX=0
BUT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0

# This test exercise multi-pack bitmap functionality where the object order is
# stored and read from a special chunk within the MIDX, so use the default
# behavior here.
sane_unset BUT_TEST_MIDX_WRITE_REV
sane_unset BUT_TEST_MIDX_READ_RIDX

midx_bitmap_core

bitmap_reuse_tests() {
	from=$1
	to=$2

	test_expect_success "setup pack reuse tests ($from -> $to)" '
		rm -fr repo &&
		but init repo &&
		(
			cd repo &&
			test_cummit_bulk 16 &&
			but tag old-tip &&

			but config core.multiPackIndex true &&
			if test "MIDX" = "$from"
			then
				but repack -Ad &&
				but multi-pack-index write --bitmap
			else
				but repack -Adb
			fi
		)
	'

	test_expect_success "build bitmap from existing ($from -> $to)" '
		(
			cd repo &&
			test_cummit_bulk --id=further 16 &&
			but tag new-tip &&

			if test "MIDX" = "$to"
			then
				but repack -d &&
				but multi-pack-index write --bitmap
			else
				but repack -Adb
			fi
		)
	'

	test_expect_success "verify resulting bitmaps ($from -> $to)" '
		(
			cd repo &&
			but for-each-ref &&
			but rev-list --test-bitmap refs/tags/old-tip &&
			but rev-list --test-bitmap refs/tags/new-tip
		)
	'
}

bitmap_reuse_tests 'pack' 'MIDX'
bitmap_reuse_tests 'MIDX' 'pack'
bitmap_reuse_tests 'MIDX' 'MIDX'

test_expect_success 'missing object closure fails gracefully' '
	rm -fr repo &&
	but init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_cummit loose &&
		test_cummit packed &&

		# Do not pass "--revs"; we want a pack without the "loose"
		# cummit.
		but pack-objects $objdir/pack/pack <<-EOF &&
		$(but rev-parse packed)
		EOF

		test_must_fail but multi-pack-index write --bitmap 2>err &&
		grep "doesn.t have full closure" err &&
		test_path_is_missing $midx
	)
'

midx_bitmap_partial_tests

test_expect_success 'removing a MIDX clears stale bitmaps' '
	rm -fr repo &&
	but init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&
		test_cummit base &&
		but repack &&
		but multi-pack-index write --bitmap &&

		# Write a MIDX and bitmap; remove the MIDX but leave the bitmap.
		stale_bitmap=$midx-$(midx_checksum $objdir).bitmap &&
		rm $midx &&

		# Then write a new MIDX.
		test_cummit new &&
		but repack &&
		but multi-pack-index write --bitmap &&

		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&
		test_path_is_missing $stale_bitmap
	)
'

test_expect_success 'pack.preferBitmapTips' '
	but init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_cummit_bulk --message="%s" 103 &&

		but log --format="%H" >cummits.raw &&
		sort <cummits.raw >cummits &&

		but log --format="create refs/tags/%s %H" HEAD >refs &&
		but update-ref --stdin <refs &&

		but multi-pack-index write --bitmap &&
		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&

		test-tool bitmap list-cummits | sort >bitmaps &&
		comm -13 bitmaps cummits >before &&
		test_line_count = 1 before &&

		perl -ne "printf(\"create refs/tags/include/%d \", $.); print" \
			<before | but update-ref --stdin &&

		rm -fr $midx-$(midx_checksum $objdir).bitmap &&
		rm -fr $midx &&

		but -c pack.preferBitmapTips=refs/tags/include \
			multi-pack-index write --bitmap &&
		test-tool bitmap list-cummits | sort >bitmaps &&
		comm -13 bitmaps cummits >after &&

		! test_cmp before after
	)
'

test_expect_success 'writing a bitmap with --refs-snapshot' '
	but init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_cummit one &&
		test_cummit two &&

		but rev-parse one >snapshot &&

		but repack -ad &&

		# First, write a MIDX which see both refs/tags/one and
		# refs/tags/two (causing both of those cummits to receive
		# bitmaps).
		but multi-pack-index write --bitmap &&

		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&

		test-tool bitmap list-cummits | sort >bitmaps &&
		grep "$(but rev-parse one)" bitmaps &&
		grep "$(but rev-parse two)" bitmaps &&

		rm -fr $midx-$(midx_checksum $objdir).bitmap &&
		rm -fr $midx &&

		# Then again, but with a refs snapshot which only sees
		# refs/tags/one.
		but multi-pack-index write --bitmap --refs-snapshot=snapshot &&

		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&

		test-tool bitmap list-cummits | sort >bitmaps &&
		grep "$(but rev-parse one)" bitmaps &&
		! grep "$(but rev-parse two)" bitmaps
	)
'

test_expect_success 'write a bitmap with --refs-snapshot (preferred tips)' '
	but init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_cummit_bulk --message="%s" 103 &&

		but log --format="%H" >cummits.raw &&
		sort <cummits.raw >cummits &&

		but log --format="create refs/tags/%s %H" HEAD >refs &&
		but update-ref --stdin <refs &&

		but multi-pack-index write --bitmap &&
		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&

		test-tool bitmap list-cummits | sort >bitmaps &&
		comm -13 bitmaps cummits >before &&
		test_line_count = 1 before &&

		(
			grep -vf before cummits.raw &&
			# mark missing cummits as preferred
			sed "s/^/+/" before
		) >snapshot &&

		rm -fr $midx-$(midx_checksum $objdir).bitmap &&
		rm -fr $midx &&

		but multi-pack-index write --bitmap --refs-snapshot=snapshot &&
		test-tool bitmap list-cummits | sort >bitmaps &&
		comm -13 bitmaps cummits >after &&

		! test_cmp before after
	)
'

test_expect_success 'hash-cache values are propagated from pack bitmaps' '
	rm -fr repo &&
	but init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_cummit base &&
		test_cummit base2 &&
		but repack -adb &&

		test-tool bitmap dump-hashes >pack.raw &&
		test_file_not_empty pack.raw &&
		sort pack.raw >pack.hashes &&

		test_cummit new &&
		but repack &&
		but multi-pack-index write --bitmap &&

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
	but init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		empty="$(but pack-objects $objdir/pack/pack </dev/null)" &&
		cat >packs <<-EOF &&
		pack-$empty.idx
		EOF

		but multi-pack-index write --bitmap --stdin-packs \
			<packs 2>err &&

		grep "bitmap without any objects" err &&

		test_path_is_file $midx &&
		test_path_is_missing $midx-$(midx_checksum $objdir).bitmap
	)
'

test_expect_success 'graceful fallback when missing reverse index' '
	rm -fr repo &&
	but init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_cummit base &&

		# write a pack and MIDX bitmap containing base
		but repack -adb &&
		but multi-pack-index write --bitmap &&

		BUT_TEST_MIDX_READ_RIDX=0 \
			but rev-list --use-bitmap-index HEAD 2>err &&
		! grep "ignoring extra bitmap file" err
	)
'

test_done
