#!/bin/sh

test_description='exercise basic bitmap functionality'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-bitmap.sh

# t5310 deals only with single-pack bitmaps, so don't write MIDX bitmaps in
# their place.
GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0

objpath () {
	echo ".but/objects/$(echo "$1" | sed -e 's|\(..\)|\1/|')"
}

# show objects present in pack ($1 should be associated *.idx)
list_packed_objects () {
	but show-index <"$1" >object-list &&
	cut -d' ' -f2 object-list
}

# has_any pattern-file content-file
# tests whether content-file has any entry from pattern-file with entries being
# whole lines.
has_any () {
	grep -Ff "$1" "$2"
}

setup_bitmap_history

test_expect_success 'setup writing bitmaps during repack' '
	but config repack.writeBitmaps true
'

test_expect_success 'full repack creates bitmaps' '
	GIT_TRACE2_EVENT="$(pwd)/trace" \
		but repack -ad &&
	ls .but/objects/pack/ | grep bitmap >output &&
	test_line_count = 1 output &&
	grep "\"key\":\"num_selected_cummits\",\"value\":\"106\"" trace &&
	grep "\"key\":\"num_maximal_cummits\",\"value\":\"107\"" trace
'

basic_bitmap_tests

test_expect_success 'incremental repack fails when bitmaps are requested' '
	test_cummit more-1 &&
	test_must_fail but repack -d 2>err &&
	test_i18ngrep "Incremental repacks are incompatible with bitmap" err
'

test_expect_success 'incremental repack can disable bitmaps' '
	test_cummit more-2 &&
	but repack -d --no-write-bitmap-index
'

test_expect_success 'pack-objects respects --local (non-local loose)' '
	but init --bare alt.but &&
	echo $(pwd)/alt.but/objects >.but/objects/info/alternates &&
	echo content1 >file1 &&
	# non-local loose object which is not present in bitmapped pack
	altblob=$(GIT_DIR=alt.but but hash-object -w file1) &&
	# non-local loose object which is also present in bitmapped pack
	but cat-file blob $blob | GIT_DIR=alt.but but hash-object -w --stdin &&
	but add file1 &&
	test_tick &&
	but cummit -m cummit_file1 &&
	echo HEAD | but pack-objects --local --stdout --revs >1.pack &&
	but index-pack 1.pack &&
	list_packed_objects 1.idx >1.objects &&
	printf "%s\n" "$altblob" "$blob" >nonlocal-loose &&
	! has_any nonlocal-loose 1.objects
'

test_expect_success 'pack-objects respects --honor-pack-keep (local non-bitmapped pack)' '
	echo content2 >file2 &&
	blob2=$(but hash-object -w file2) &&
	but add file2 &&
	test_tick &&
	but cummit -m cummit_file2 &&
	printf "%s\n" "$blob2" "$bitmaptip" >keepobjects &&
	pack2=$(but pack-objects pack2 <keepobjects) &&
	mv pack2-$pack2.* .but/objects/pack/ &&
	>.but/objects/pack/pack2-$pack2.keep &&
	rm $(objpath $blob2) &&
	echo HEAD | but pack-objects --honor-pack-keep --stdout --revs >2a.pack &&
	but index-pack 2a.pack &&
	list_packed_objects 2a.idx >2a.objects &&
	! has_any keepobjects 2a.objects
'

test_expect_success 'pack-objects respects --local (non-local pack)' '
	mv .but/objects/pack/pack2-$pack2.* alt.but/objects/pack/ &&
	echo HEAD | but pack-objects --local --stdout --revs >2b.pack &&
	but index-pack 2b.pack &&
	list_packed_objects 2b.idx >2b.objects &&
	! has_any keepobjects 2b.objects
'

test_expect_success 'pack-objects respects --honor-pack-keep (local bitmapped pack)' '
	ls .but/objects/pack/ | grep bitmap >output &&
	test_line_count = 1 output &&
	packbitmap=$(basename $(cat output) .bitmap) &&
	list_packed_objects .but/objects/pack/$packbitmap.idx >packbitmap.objects &&
	test_when_finished "rm -f .but/objects/pack/$packbitmap.keep" &&
	>.but/objects/pack/$packbitmap.keep &&
	echo HEAD | but pack-objects --honor-pack-keep --stdout --revs >3a.pack &&
	but index-pack 3a.pack &&
	list_packed_objects 3a.idx >3a.objects &&
	! has_any packbitmap.objects 3a.objects
'

test_expect_success 'pack-objects respects --local (non-local bitmapped pack)' '
	mv .but/objects/pack/$packbitmap.* alt.but/objects/pack/ &&
	rm -f .but/objects/pack/multi-pack-index &&
	test_when_finished "mv alt.but/objects/pack/$packbitmap.* .but/objects/pack/" &&
	echo HEAD | but pack-objects --local --stdout --revs >3b.pack &&
	but index-pack 3b.pack &&
	list_packed_objects 3b.idx >3b.objects &&
	! has_any packbitmap.objects 3b.objects
'

test_expect_success 'pack-objects to file can use bitmap' '
	# make sure we still have 1 bitmap index from previous tests
	ls .but/objects/pack/ | grep bitmap >output &&
	test_line_count = 1 output &&
	# verify equivalent packs are generated with/without using bitmap index
	packasha1=$(but pack-objects --no-use-bitmap-index --all packa </dev/null) &&
	packbsha1=$(but pack-objects --use-bitmap-index --all packb </dev/null) &&
	list_packed_objects packa-$packasha1.idx >packa.objects &&
	list_packed_objects packb-$packbsha1.idx >packb.objects &&
	test_cmp packa.objects packb.objects
'

test_expect_success 'full repack, reusing previous bitmaps' '
	but repack -ad &&
	ls .but/objects/pack/ | grep bitmap >output &&
	test_line_count = 1 output
'

test_expect_success 'fetch (full bitmap)' '
	but --but-dir=clone.but fetch origin second:second &&
	but rev-parse HEAD >expect &&
	but --but-dir=clone.but rev-parse HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'create objects for missing-HAVE tests' '
	blob=$(echo "missing have" | but hash-object -w --stdin) &&
	tree=$(printf "100644 blob $blob\tfile\n" | but mktree) &&
	parent=$(echo parent | but cummit-tree $tree) &&
	cummit=$(echo cummit | but cummit-tree $tree -p $parent) &&
	cat >revs <<-EOF
	HEAD
	^HEAD^
	^$cummit
	EOF
'

test_expect_success 'pack-objects respects --incremental' '
	cat >revs2 <<-EOF &&
	HEAD
	$cummit
	EOF
	but pack-objects --incremental --stdout --revs <revs2 >4.pack &&
	but index-pack 4.pack &&
	list_packed_objects 4.idx >4.objects &&
	test_line_count = 4 4.objects &&
	but rev-list --objects $cummit >revlist &&
	cut -d" " -f1 revlist |sort >objects &&
	test_cmp 4.objects objects
'

test_expect_success 'pack with missing blob' '
	rm $(objpath $blob) &&
	but pack-objects --stdout --revs <revs >/dev/null
'

test_expect_success 'pack with missing tree' '
	rm $(objpath $tree) &&
	but pack-objects --stdout --revs <revs >/dev/null
'

test_expect_success 'pack with missing parent' '
	rm $(objpath $parent) &&
	but pack-objects --stdout --revs <revs >/dev/null
'

test_expect_success JGIT,SHA1 'we can read jbut bitmaps' '
	but clone --bare . compat-jbut.but &&
	(
		cd compat-jbut.but &&
		rm -f objects/pack/*.bitmap &&
		jbut gc &&
		but rev-list --test-bitmap HEAD
	)
'

test_expect_success JGIT,SHA1 'jbut can read our bitmaps' '
	but clone --bare . compat-us.but &&
	(
		cd compat-us.but &&
		but repack -adb &&
		# jbut gc will barf if it does not like our bitmaps
		jbut gc
	)
'

test_expect_success 'splitting packs does not generate bogus bitmaps' '
	test-tool genrandom foo $((1024 * 1024)) >rand &&
	but add rand &&
	but cummit -m "cummit with big file" &&
	but -c pack.packSizeLimit=500k repack -adb &&
	but init --bare no-bitmaps.but &&
	but -C no-bitmaps.but fetch .. HEAD
'

test_expect_success 'set up reusable pack' '
	rm -f .but/objects/pack/*.keep &&
	but repack -adb &&
	reusable_pack () {
		but for-each-ref --format="%(objectname)" |
		but pack-objects --delta-base-offset --revs --stdout "$@"
	}
'

test_expect_success 'pack reuse respects --honor-pack-keep' '
	test_when_finished "rm -f .but/objects/pack/*.keep" &&
	for i in .but/objects/pack/*.pack
	do
		>${i%.pack}.keep || return 1
	done &&
	reusable_pack --honor-pack-keep >empty.pack &&
	but index-pack empty.pack &&
	but show-index <empty.idx >actual &&
	test_must_be_empty actual
'

test_expect_success 'pack reuse respects --local' '
	mv .but/objects/pack/* alt.but/objects/pack/ &&
	test_when_finished "mv alt.but/objects/pack/* .but/objects/pack/" &&
	reusable_pack --local >empty.pack &&
	but index-pack empty.pack &&
	but show-index <empty.idx >actual &&
	test_must_be_empty actual
'

test_expect_success 'pack reuse respects --incremental' '
	reusable_pack --incremental >empty.pack &&
	but index-pack empty.pack &&
	but show-index <empty.idx >actual &&
	test_must_be_empty actual
'

test_expect_success 'truncated bitmap fails gracefully (ewah)' '
	test_config pack.writebitmaphashcache false &&
	but repack -ad &&
	but rev-list --use-bitmap-index --count --all >expect &&
	bitmap=$(ls .but/objects/pack/*.bitmap) &&
	test_when_finished "rm -f $bitmap" &&
	test_copy_bytes 256 <$bitmap >$bitmap.tmp &&
	mv -f $bitmap.tmp $bitmap &&
	but rev-list --use-bitmap-index --count --all >actual 2>stderr &&
	test_cmp expect actual &&
	test_i18ngrep corrupt.ewah.bitmap stderr
'

test_expect_success 'truncated bitmap fails gracefully (cache)' '
	but repack -ad &&
	but rev-list --use-bitmap-index --count --all >expect &&
	bitmap=$(ls .but/objects/pack/*.bitmap) &&
	test_when_finished "rm -f $bitmap" &&
	test_copy_bytes 512 <$bitmap >$bitmap.tmp &&
	mv -f $bitmap.tmp $bitmap &&
	but rev-list --use-bitmap-index --count --all >actual 2>stderr &&
	test_cmp expect actual &&
	test_i18ngrep corrupted.bitmap.index stderr
'

# Create a state of history with these properties:
#
#  - refs that allow a client to fetch some new history, while sharing some old
#    history with the server; we use branches delta-reuse-old and
#    delta-reuse-new here
#
#  - the new history contains an object that is stored on the server as a delta
#    against a base that is in the old history
#
#  - the base object is not immediately reachable from the tip of the old
#    history; finding it would involve digging down through history we know the
#    other side has
#
# This should result in a state where fetching from old->new would not
# traditionally reuse the on-disk delta (because we'd have to dig to realize
# that the client has it), but we will do so if bitmaps can tell us cheaply
# that the other side has it.
test_expect_success 'set up thin delta-reuse parent' '
	# This first cummit contains the buried base object.
	test-tool genrandom delta 16384 >file &&
	but add file &&
	but cummit -m "delta base" &&
	base=$(but rev-parse --verify HEAD:file) &&

	# These intermediate cummits bury the base back in history.
	# This becomes the "old" state.
	for i in 1 2 3 4 5
	do
		echo $i >file &&
		but cummit -am "intermediate $i" || return 1
	done &&
	but branch delta-reuse-old &&

	# And now our new history has a delta against the buried base. Note
	# that this must be smaller than the original file, since pack-objects
	# prefers to create deltas from smaller objects to larger.
	test-tool genrandom delta 16300 >file &&
	but cummit -am "delta result" &&
	delta=$(but rev-parse --verify HEAD:file) &&
	but branch delta-reuse-new &&

	# Repack with bitmaps and double check that we have the expected delta
	# relationship.
	but repack -adb &&
	have_delta $delta $base
'

# Now we can sanity-check the non-bitmap behavior (that the server is not able
# to reuse the delta). This isn't strictly something we care about, so this
# test could be scrapped in the future. But it makes sure that the next test is
# actually triggering the feature we want.
#
# Note that our tools for working with on-the-wire "thin" packs are limited. So
# we actually perform the fetch, retain the resulting pack, and inspect the
# result.
test_expect_success 'fetch without bitmaps ignores delta against old base' '
	test_config pack.usebitmaps false &&
	test_when_finished "rm -rf client.but" &&
	but init --bare client.but &&
	(
		cd client.but &&
		but config transfer.unpackLimit 1 &&
		but fetch .. delta-reuse-old:delta-reuse-old &&
		but fetch .. delta-reuse-new:delta-reuse-new &&
		have_delta $delta $ZERO_OID
	)
'

# And do the same for the bitmap case, where we do expect to find the delta.
test_expect_success 'fetch with bitmaps can reuse old base' '
	test_config pack.usebitmaps true &&
	test_when_finished "rm -rf client.but" &&
	but init --bare client.but &&
	(
		cd client.but &&
		but config transfer.unpackLimit 1 &&
		but fetch .. delta-reuse-old:delta-reuse-old &&
		but fetch .. delta-reuse-new:delta-reuse-new &&
		have_delta $delta $base
	)
'

test_expect_success 'pack.preferBitmapTips' '
	but init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		# create enough cummits that not all are receive bitmap
		# coverage even if they are all at the tip of some reference.
		test_cummit_bulk --message="%s" 103 &&

		but rev-list HEAD >cummits.raw &&
		sort <cummits.raw >cummits &&

		but log --format="create refs/tags/%s %H" HEAD >refs &&
		but update-ref --stdin <refs &&

		but repack -adb &&
		test-tool bitmap list-cummits | sort >bitmaps &&

		# remember which cummits did not receive bitmaps
		comm -13 bitmaps cummits >before &&
		test_file_not_empty before &&

		# mark the cummits which did not receive bitmaps as preferred,
		# and generate the bitmap again
		perl -pe "s{^}{create refs/tags/include/$. }" <before |
			but update-ref --stdin &&
		but -c pack.preferBitmapTips=refs/tags/include repack -adb &&

		# finally, check that the cummit(s) without bitmap coverage
		# are not the same ones as before
		test-tool bitmap list-cummits | sort >bitmaps &&
		comm -13 bitmaps cummits >after &&

		! test_cmp before after
	)
'

test_expect_success 'complains about multiple pack bitmaps' '
	rm -fr repo &&
	but init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_cummit base &&

		but repack -adb &&
		bitmap="$(ls .but/objects/pack/pack-*.bitmap)" &&
		mv "$bitmap" "$bitmap.bak" &&

		test_cummit other &&
		but repack -ab &&

		mv "$bitmap.bak" "$bitmap" &&

		find .but/objects/pack -type f -name "*.pack" >packs &&
		find .but/objects/pack -type f -name "*.bitmap" >bitmaps &&
		test_line_count = 2 packs &&
		test_line_count = 2 bitmaps &&

		but rev-list --use-bitmap-index HEAD 2>err &&
		grep "ignoring extra bitmap file" err
	)
'

test_done
