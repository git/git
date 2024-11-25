#!/bin/sh

test_description='exercise basic bitmap functionality'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-bitmap.sh

# Likewise, allow individual tests to control whether or not they use
# the boundary-based traversal.
sane_unset GIT_TEST_PACK_USE_BITMAP_BOUNDARY_TRAVERSAL

objpath () {
	echo ".git/objects/$(echo "$1" | sed -e 's|\(..\)|\1/|')"
}

# show objects present in pack ($1 should be associated *.idx)
list_packed_objects () {
	git show-index <"$1" >object-list &&
	cut -d' ' -f2 object-list
}

# has_any pattern-file content-file
# tests whether content-file has any entry from pattern-file with entries being
# whole lines.
has_any () {
	grep -Ff "$1" "$2"
}

test_bitmap_cases () {
	writeLookupTable=false
	for i in "$@"
	do
		case "$i" in
		"pack.writeBitmapLookupTable") writeLookupTable=true;;
		esac
	done

	test_expect_success 'setup test repository' '
		rm -fr * .git &&
		git init &&
		git config pack.writeBitmapLookupTable '"$writeLookupTable"'
	'
	setup_bitmap_history

	test_expect_success 'setup writing bitmaps during repack' '
		git config repack.writeBitmaps true
	'

	test_expect_success 'full repack creates bitmaps' '
		GIT_TRACE2_EVENT="$(pwd)/trace" \
			git repack -ad &&
		ls .git/objects/pack/ | grep bitmap >output &&
		test_line_count = 1 output &&
		grep "\"key\":\"num_selected_commits\",\"value\":\"106\"" trace &&
		grep "\"key\":\"num_maximal_commits\",\"value\":\"107\"" trace
	'

	basic_bitmap_tests

	test_expect_success 'pack-objects respects --local (non-local loose)' '
		git init --bare alt.git &&
		echo $(pwd)/alt.git/objects >.git/objects/info/alternates &&
		echo content1 >file1 &&
		# non-local loose object which is not present in bitmapped pack
		altblob=$(GIT_DIR=alt.git git hash-object -w file1) &&
		# non-local loose object which is also present in bitmapped pack
		git cat-file blob $blob | GIT_DIR=alt.git git hash-object -w --stdin &&
		git add file1 &&
		test_tick &&
		git commit -m commit_file1 &&
		echo HEAD | git pack-objects --local --stdout --revs >1.pack &&
		git index-pack 1.pack &&
		list_packed_objects 1.idx >1.objects &&
		printf "%s\n" "$altblob" "$blob" >nonlocal-loose &&
		! has_any nonlocal-loose 1.objects
	'

	test_expect_success 'pack-objects respects --honor-pack-keep (local non-bitmapped pack)' '
		echo content2 >file2 &&
		blob2=$(git hash-object -w file2) &&
		git add file2 &&
		test_tick &&
		git commit -m commit_file2 &&
		printf "%s\n" "$blob2" "$bitmaptip" >keepobjects &&
		pack2=$(git pack-objects pack2 <keepobjects) &&
		mv pack2-$pack2.* .git/objects/pack/ &&
		>.git/objects/pack/pack2-$pack2.keep &&
		rm $(objpath $blob2) &&
		echo HEAD | git pack-objects --honor-pack-keep --stdout --revs >2a.pack &&
		git index-pack 2a.pack &&
		list_packed_objects 2a.idx >2a.objects &&
		! has_any keepobjects 2a.objects
	'

	test_expect_success 'pack-objects respects --local (non-local pack)' '
		mv .git/objects/pack/pack2-$pack2.* alt.git/objects/pack/ &&
		echo HEAD | git pack-objects --local --stdout --revs >2b.pack &&
		git index-pack 2b.pack &&
		list_packed_objects 2b.idx >2b.objects &&
		! has_any keepobjects 2b.objects
	'

	test_expect_success 'pack-objects respects --honor-pack-keep (local bitmapped pack)' '
		ls .git/objects/pack/ | grep bitmap >output &&
		test_line_count = 1 output &&
		packbitmap=$(basename $(cat output) .bitmap) &&
		list_packed_objects .git/objects/pack/$packbitmap.idx >packbitmap.objects &&
		test_when_finished "rm -f .git/objects/pack/$packbitmap.keep" &&
		>.git/objects/pack/$packbitmap.keep &&
		echo HEAD | git pack-objects --honor-pack-keep --stdout --revs >3a.pack &&
		git index-pack 3a.pack &&
		list_packed_objects 3a.idx >3a.objects &&
		! has_any packbitmap.objects 3a.objects
	'

	test_expect_success 'pack-objects respects --local (non-local bitmapped pack)' '
		mv .git/objects/pack/$packbitmap.* alt.git/objects/pack/ &&
		rm -f .git/objects/pack/multi-pack-index &&
		test_when_finished "mv alt.git/objects/pack/$packbitmap.* .git/objects/pack/" &&
		echo HEAD | git pack-objects --local --stdout --revs >3b.pack &&
		git index-pack 3b.pack &&
		list_packed_objects 3b.idx >3b.objects &&
		! has_any packbitmap.objects 3b.objects
	'

	test_expect_success 'pack-objects to file can use bitmap' '
		# make sure we still have 1 bitmap index from previous tests
		ls .git/objects/pack/ | grep bitmap >output &&
		test_line_count = 1 output &&
		# verify equivalent packs are generated with/without using bitmap index
		packasha1=$(git pack-objects --no-use-bitmap-index --all packa </dev/null) &&
		packbsha1=$(git pack-objects --use-bitmap-index --all packb </dev/null) &&
		list_packed_objects packa-$packasha1.idx >packa.objects &&
		list_packed_objects packb-$packbsha1.idx >packb.objects &&
		test_cmp packa.objects packb.objects
	'

	test_expect_success 'full repack, reusing previous bitmaps' '
		git repack -ad &&
		ls .git/objects/pack/ | grep bitmap >output &&
		test_line_count = 1 output
	'

	test_expect_success 'fetch (full bitmap)' '
		git --git-dir=clone.git fetch origin second:second &&
		git rev-parse HEAD >expect &&
		git --git-dir=clone.git rev-parse HEAD >actual &&
		test_cmp expect actual
	'

	test_expect_success 'create objects for missing-HAVE tests' '
		blob=$(echo "missing have" | git hash-object -w --stdin) &&
		tree=$(printf "100644 blob $blob\tfile\n" | git mktree) &&
		parent=$(echo parent | git commit-tree $tree) &&
		commit=$(echo commit | git commit-tree $tree -p $parent) &&
		cat >revs <<-EOF
		HEAD
		^HEAD^
		^$commit
		EOF
	'

	test_expect_success 'pack-objects respects --incremental' '
		cat >revs2 <<-EOF &&
		HEAD
		$commit
		EOF
		git pack-objects --incremental --stdout --revs <revs2 >4.pack &&
		git index-pack 4.pack &&
		list_packed_objects 4.idx >4.objects &&
		test_line_count = 4 4.objects &&
		git rev-list --objects $commit >revlist &&
		cut -d" " -f1 revlist |sort >objects &&
		test_cmp 4.objects objects
	'

	test_expect_success 'pack with missing blob' '
		rm $(objpath $blob) &&
		git pack-objects --stdout --revs <revs >/dev/null
	'

	test_expect_success 'pack with missing tree' '
		rm $(objpath $tree) &&
		git pack-objects --stdout --revs <revs >/dev/null
	'

	test_expect_success 'pack with missing parent' '
		rm $(objpath $parent) &&
		git pack-objects --stdout --revs <revs >/dev/null
	'

	test_expect_success JGIT,SHA1 'we can read jgit bitmaps' '
		git clone --bare . compat-jgit.git &&
		(
			cd compat-jgit.git &&
			rm -f objects/pack/*.bitmap &&
			jgit gc &&
			git rev-list --test-bitmap HEAD
		)
	'

	test_expect_success JGIT,SHA1 'jgit can read our bitmaps' '
		git clone --bare . compat-us.git &&
		(
			cd compat-us.git &&
			git config pack.writeBitmapLookupTable '"$writeLookupTable"' &&
			git repack -adb &&
			# jgit gc will barf if it does not like our bitmaps
			jgit gc
		)
	'

	test_expect_success 'splitting packs does not generate bogus bitmaps' '
		test-tool genrandom foo $((1024 * 1024)) >rand &&
		git add rand &&
		git commit -m "commit with big file" &&
		git -c pack.packSizeLimit=500k repack -adb &&
		git init --bare no-bitmaps.git &&
		git -C no-bitmaps.git fetch .. HEAD
	'

	test_expect_success 'set up reusable pack' '
		rm -f .git/objects/pack/*.keep &&
		git repack -adb &&
		reusable_pack () {
			git for-each-ref --format="%(objectname)" |
			git pack-objects --delta-base-offset --revs --stdout "$@"
		}
	'

	test_expect_success 'pack reuse respects --honor-pack-keep' '
		test_when_finished "rm -f .git/objects/pack/*.keep" &&
		for i in .git/objects/pack/*.pack
		do
			>${i%.pack}.keep || return 1
		done &&
		reusable_pack --honor-pack-keep >empty.pack &&
		git index-pack empty.pack &&
		git show-index <empty.idx >actual &&
		test_must_be_empty actual
	'

	test_expect_success 'pack reuse respects --local' '
		mv .git/objects/pack/* alt.git/objects/pack/ &&
		test_when_finished "mv alt.git/objects/pack/* .git/objects/pack/" &&
		reusable_pack --local >empty.pack &&
		git index-pack empty.pack &&
		git show-index <empty.idx >actual &&
		test_must_be_empty actual
	'

	test_expect_success 'pack reuse respects --incremental' '
		reusable_pack --incremental >empty.pack &&
		git index-pack empty.pack &&
		git show-index <empty.idx >actual &&
		test_must_be_empty actual
	'

	test_expect_success 'truncated bitmap fails gracefully (ewah)' '
		test_config pack.writebitmaphashcache false &&
		test_config pack.writebitmaplookuptable false &&
		git repack -ad &&
		git rev-list --use-bitmap-index --count --all >expect &&
		bitmap=$(ls .git/objects/pack/*.bitmap) &&
		test_when_finished "rm -f $bitmap" &&
		test_copy_bytes 256 <$bitmap >$bitmap.tmp &&
		mv -f $bitmap.tmp $bitmap &&
		git rev-list --use-bitmap-index --count --all >actual 2>stderr &&
		test_cmp expect actual &&
		test_grep corrupt.ewah.bitmap stderr
	'

	test_expect_success 'truncated bitmap fails gracefully (cache)' '
		git config pack.writeBitmapLookupTable '"$writeLookupTable"' &&
		git repack -ad &&
		git rev-list --use-bitmap-index --count --all >expect &&
		bitmap=$(ls .git/objects/pack/*.bitmap) &&
		test_when_finished "rm -f $bitmap" &&
		test_copy_bytes 512 <$bitmap >$bitmap.tmp &&
		mv -f $bitmap.tmp $bitmap &&
		git rev-list --use-bitmap-index --count --all >actual 2>stderr &&
		test_cmp expect actual &&
		test_grep corrupted.bitmap.index stderr
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
		# This first commit contains the buried base object.
		test-tool genrandom delta 16384 >file &&
		git add file &&
		git commit -m "delta base" &&
		base=$(git rev-parse --verify HEAD:file) &&

		# These intermediate commits bury the base back in history.
		# This becomes the "old" state.
		for i in 1 2 3 4 5
		do
			echo $i >file &&
			git commit -am "intermediate $i" || return 1
		done &&
		git branch delta-reuse-old &&

		# And now our new history has a delta against the buried base. Note
		# that this must be smaller than the original file, since pack-objects
		# prefers to create deltas from smaller objects to larger.
		test-tool genrandom delta 16300 >file &&
		git commit -am "delta result" &&
		delta=$(git rev-parse --verify HEAD:file) &&
		git branch delta-reuse-new &&

		# Repack with bitmaps and double check that we have the expected delta
		# relationship.
		git repack -adb &&
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
		test_when_finished "rm -rf client.git" &&
		git init --bare client.git &&
		(
			cd client.git &&
			git config transfer.unpackLimit 1 &&
			git fetch .. delta-reuse-old:delta-reuse-old &&
			git fetch .. delta-reuse-new:delta-reuse-new &&
			have_delta $delta $ZERO_OID
		)
	'

	# And do the same for the bitmap case, where we do expect to find the delta.
	test_expect_success 'fetch with bitmaps can reuse old base' '
		test_config pack.usebitmaps true &&
		test_when_finished "rm -rf client.git" &&
		git init --bare client.git &&
		(
			cd client.git &&
			git config transfer.unpackLimit 1 &&
			git fetch .. delta-reuse-old:delta-reuse-old &&
			git fetch .. delta-reuse-new:delta-reuse-new &&
			have_delta $delta $base
		)
	'

	test_expect_success 'pack.preferBitmapTips' '
		git init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&
			git config pack.writeBitmapLookupTable '"$writeLookupTable"' &&

			# create enough commits that not all are receive bitmap
			# coverage even if they are all at the tip of some reference.
			test_commit_bulk --message="%s" 103 &&

			git rev-list HEAD >commits.raw &&
			sort <commits.raw >commits &&

			git log --format="create refs/tags/%s %H" HEAD >refs &&
			git update-ref --stdin <refs &&

			git repack -adb &&
			test-tool bitmap list-commits | sort >bitmaps &&

			# remember which commits did not receive bitmaps
			comm -13 bitmaps commits >before &&
			test_file_not_empty before &&

			# mark the commits which did not receive bitmaps as preferred,
			# and generate the bitmap again
			perl -pe "s{^}{create refs/tags/include/$. }" <before |
				git update-ref --stdin &&
			git -c pack.preferBitmapTips=refs/tags/include repack -adb &&

			# finally, check that the commit(s) without bitmap coverage
			# are not the same ones as before
			test-tool bitmap list-commits | sort >bitmaps &&
			comm -13 bitmaps commits >after &&

			! test_cmp before after
		)
	'

	test_expect_success 'pack.preferBitmapTips' '
		git init repo &&
		test_when_finished "rm -rf repo" &&
		(
			cd repo &&
			git config pack.writeBitmapLookupTable '"$writeLookupTable"' &&
			test_commit_bulk --message="%s" 103 &&

			cat >>.git/config <<-\EOF &&
			[pack]
				preferBitmapTips
			EOF
			cat >expect <<-\EOF &&
			error: missing value for '\''pack.preferbitmaptips'\''
			EOF
			git repack -adb 2>actual &&
			test_cmp expect actual
		)
	'

	test_expect_success 'complains about multiple pack bitmaps' '
		rm -fr repo &&
		git init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&
			git config pack.writeBitmapLookupTable '"$writeLookupTable"' &&

			test_commit base &&

			git repack -adb &&
			bitmap="$(ls .git/objects/pack/pack-*.bitmap)" &&
			mv "$bitmap" "$bitmap.bak" &&

			test_commit other &&
			git repack -ab &&

			mv "$bitmap.bak" "$bitmap" &&

			find .git/objects/pack -type f -name "*.pack" >packs &&
			find .git/objects/pack -type f -name "*.bitmap" >bitmaps &&
			test_line_count = 2 packs &&
			test_line_count = 2 bitmaps &&

			GIT_TRACE2_EVENT=$(pwd)/trace2.txt git rev-list --use-bitmap-index HEAD &&
			grep "opened bitmap" trace2.txt &&
			grep "ignoring extra bitmap" trace2.txt
		)
	'
}

test_bitmap_cases

GIT_TEST_PACK_USE_BITMAP_BOUNDARY_TRAVERSAL=1
export GIT_TEST_PACK_USE_BITMAP_BOUNDARY_TRAVERSAL

test_bitmap_cases

sane_unset GIT_TEST_PACK_USE_BITMAP_BOUNDARY_TRAVERSAL

test_expect_success 'incremental repack fails when bitmaps are requested' '
	test_commit more-1 &&
	test_must_fail git repack -d 2>err &&
	test_grep "Incremental repacks are incompatible with bitmap" err
'

test_expect_success 'incremental repack can disable bitmaps' '
	test_commit more-2 &&
	git repack -d --no-write-bitmap-index
'

test_expect_success 'boundary-based traversal is used when requested' '
	git repack -a -d --write-bitmap-index &&

	for argv in \
		"git -c pack.useBitmapBoundaryTraversal=true" \
		"git -c feature.experimental=true" \
		"GIT_TEST_PACK_USE_BITMAP_BOUNDARY_TRAVERSAL=1 git"
	do
		eval "GIT_TRACE2_EVENT=1 $argv rev-list --objects \
			--use-bitmap-index second..other 2>perf" &&
		grep "\"region_enter\".*\"label\":\"haves/boundary\"" perf ||
			return 1
	done &&

	for argv in \
		"git -c pack.useBitmapBoundaryTraversal=false" \
		"git -c feature.experimental=true -c pack.useBitmapBoundaryTraversal=false" \
		"GIT_TEST_PACK_USE_BITMAP_BOUNDARY_TRAVERSAL=0 git -c pack.useBitmapBoundaryTraversal=true" \
		"GIT_TEST_PACK_USE_BITMAP_BOUNDARY_TRAVERSAL=0 git -c feature.experimental=true"
	do
		eval "GIT_TRACE2_EVENT=1 $argv rev-list --objects \
			--use-bitmap-index second..other 2>perf" &&
		grep "\"region_enter\".*\"label\":\"haves/classic\"" perf ||
			return 1
	done
'

test_expect_success 'left-right not confused by bitmap index' '
	git rev-list --left-right other...HEAD >expect &&
	git rev-list --use-bitmap-index --left-right other...HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'left-right count not confused by bitmap-index' '
	git rev-list --left-right --count other...HEAD >expect &&
	git rev-list --use-bitmap-index --left-right --count other...HEAD >actual &&
	test_cmp expect actual
'

test_bitmap_cases "pack.writeBitmapLookupTable"

test_expect_success 'verify writing bitmap lookup table when enabled' '
	GIT_TRACE2_EVENT="$(pwd)/trace2" \
		git repack -ad &&
	grep "\"label\":\"writing_lookup_table\"" trace2
'

test_expect_success 'truncated bitmap fails gracefully (lookup table)' '
	test_config pack.writebitmaphashcache false &&
	git repack -adb &&
	git rev-list --use-bitmap-index --count --all >expect &&
	bitmap=$(ls .git/objects/pack/*.bitmap) &&
	test_when_finished "rm -f $bitmap" &&
	test_copy_bytes 512 <$bitmap >$bitmap.tmp &&
	mv -f $bitmap.tmp $bitmap &&
	git rev-list --use-bitmap-index --count --all >actual 2>stderr &&
	test_cmp expect actual &&
	test_grep corrupted.bitmap.index stderr
'

test_done
