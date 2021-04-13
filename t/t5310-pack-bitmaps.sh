#!/bin/sh

test_description='exercise basic bitmap functionality'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=master
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-bundle.sh
. "$TEST_DIRECTORY"/lib-bitmap.sh

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

# To ensure the logic for "maximal commits" is exercised, make
# the repository a bit more complicated.
#
#    other                         second
#      *                             *
# (99 commits)                  (99 commits)
#      *                             *
#      |\                           /|
#      | * octo-other  octo-second * |
#      |/|\_________  ____________/|\|
#      | \          \/  __________/  |
#      |  | ________/\ /             |
#      *  |/          * merge-right  *
#      | _|__________/ \____________ |
#      |/ |                         \|
# (l1) *  * merge-left               * (r1)
#      | / \________________________ |
#      |/                           \|
# (l2) *                             * (r2)
#       \___________________________ |
#                                   \|
#                                    * (base)
#
# We only push bits down the first-parent history, which
# makes some of these commits unimportant!
#
# The important part for the maximal commit algorithm is how
# the bitmasks are extended. Assuming starting bit positions
# for second (bit 0) and other (bit 1), the bitmasks at the
# end should be:
#
#      second: 1       (maximal, selected)
#       other: 01      (maximal, selected)
#      (base): 11 (maximal)
#
# This complicated history was important for a previous
# version of the walk that guarantees never walking a
# commit multiple times. That goal might be important
# again, so preserve this complicated case. For now, this
# test will guarantee that the bitmaps are computed
# correctly, even with the repeat calculations.

test_expect_success 'setup repo with moderate-sized history' '
	test_commit_bulk --id=file 10 &&
	git branch -M second &&
	git checkout -b other HEAD~5 &&
	test_commit_bulk --id=side 10 &&

	# add complicated history setup, including merges and
	# ambiguous merge-bases

	git checkout -b merge-left other~2 &&
	git merge second~2 -m "merge-left" &&

	git checkout -b merge-right second~1 &&
	git merge other~1 -m "merge-right" &&

	git checkout -b octo-second second &&
	git merge merge-left merge-right -m "octopus-second" &&

	git checkout -b octo-other other &&
	git merge merge-left merge-right -m "octopus-other" &&

	git checkout other &&
	git merge octo-other -m "pull octopus" &&

	git checkout second &&
	git merge octo-second -m "pull octopus" &&

	# Remove these branches so they are not selected
	# as bitmap tips
	git branch -D merge-left &&
	git branch -D merge-right &&
	git branch -D octo-other &&
	git branch -D octo-second &&

	# add padding to make these merges less interesting
	# and avoid having them selected for bitmaps
	test_commit_bulk --id=file 100 &&
	git checkout other &&
	test_commit_bulk --id=side 100 &&
	git checkout second &&

	bitmaptip=$(git rev-parse second) &&
	blob=$(echo tagged-blob | git hash-object -w --stdin) &&
	git tag tagged-blob $blob &&
	git config repack.writebitmaps true
'

test_expect_success 'full repack creates bitmaps' '
	GIT_TRACE2_EVENT_NESTING=4 GIT_TRACE2_EVENT="$(pwd)/trace" \
		git repack -ad &&
	ls .git/objects/pack/ | grep bitmap >output &&
	test_line_count = 1 output &&
	grep "\"key\":\"num_selected_commits\",\"value\":\"106\"" trace &&
	grep "\"key\":\"num_maximal_commits\",\"value\":\"107\"" trace
'

test_expect_success 'rev-list --test-bitmap verifies bitmaps' '
	git rev-list --test-bitmap HEAD
'

rev_list_tests_head () {
	test_expect_success "counting commits via bitmap ($state, $branch)" '
		git rev-list --count $branch >expect &&
		git rev-list --use-bitmap-index --count $branch >actual &&
		test_cmp expect actual
	'

	test_expect_success "counting partial commits via bitmap ($state, $branch)" '
		git rev-list --count $branch~5..$branch >expect &&
		git rev-list --use-bitmap-index --count $branch~5..$branch >actual &&
		test_cmp expect actual
	'

	test_expect_success "counting commits with limit ($state, $branch)" '
		git rev-list --count -n 1 $branch >expect &&
		git rev-list --use-bitmap-index --count -n 1 $branch >actual &&
		test_cmp expect actual
	'

	test_expect_success "counting non-linear history ($state, $branch)" '
		git rev-list --count other...second >expect &&
		git rev-list --use-bitmap-index --count other...second >actual &&
		test_cmp expect actual
	'

	test_expect_success "counting commits with limiting ($state, $branch)" '
		git rev-list --count $branch -- 1.t >expect &&
		git rev-list --use-bitmap-index --count $branch -- 1.t >actual &&
		test_cmp expect actual
	'

	test_expect_success "counting objects via bitmap ($state, $branch)" '
		git rev-list --count --objects $branch >expect &&
		git rev-list --use-bitmap-index --count --objects $branch >actual &&
		test_cmp expect actual
	'

	test_expect_success "enumerate commits ($state, $branch)" '
		git rev-list --use-bitmap-index $branch >actual &&
		git rev-list $branch >expect &&
		test_bitmap_traversal --no-confirm-bitmaps expect actual
	'

	test_expect_success "enumerate --objects ($state, $branch)" '
		git rev-list --objects --use-bitmap-index $branch >actual &&
		git rev-list --objects $branch >expect &&
		test_bitmap_traversal expect actual
	'

	test_expect_success "bitmap --objects handles non-commit objects ($state, $branch)" '
		git rev-list --objects --use-bitmap-index $branch tagged-blob >actual &&
		grep $blob actual
	'
}

rev_list_tests () {
	state=$1

	for branch in "second" "other"
	do
		rev_list_tests_head
	done
}

rev_list_tests 'full bitmap'

test_expect_success 'clone from bitmapped repository' '
	git clone --no-local --bare . clone.git &&
	git rev-parse HEAD >expect &&
	git --git-dir=clone.git rev-parse HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'partial clone from bitmapped repository' '
	test_config uploadpack.allowfilter true &&
	git clone --no-local --bare --filter=blob:none . partial-clone.git &&
	(
		cd partial-clone.git &&
		pack=$(echo objects/pack/*.pack) &&
		git verify-pack -v "$pack" >have &&
		awk "/blob/ { print \$1 }" <have >blobs &&
		# we expect this single blob because of the direct ref
		git rev-parse refs/tags/tagged-blob >expect &&
		test_cmp expect blobs
	)
'

test_expect_success 'setup further non-bitmapped commits' '
	test_commit_bulk --id=further 10
'

rev_list_tests 'partial bitmap'

test_expect_success 'fetch (partial bitmap)' '
	git --git-dir=clone.git fetch origin second:second &&
	git rev-parse HEAD >expect &&
	git --git-dir=clone.git rev-parse HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'incremental repack fails when bitmaps are requested' '
	test_commit more-1 &&
	test_must_fail git repack -d 2>err &&
	test_i18ngrep "Incremental repacks are incompatible with bitmap" err
'

test_expect_success 'incremental repack can disable bitmaps' '
	test_commit more-2 &&
	git repack -d --no-write-bitmap-index
'

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
		>${i%.pack}.keep
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
	git repack -ad &&
	git rev-list --use-bitmap-index --count --all >expect &&
	bitmap=$(ls .git/objects/pack/*.bitmap) &&
	test_when_finished "rm -f $bitmap" &&
	test_copy_bytes 256 <$bitmap >$bitmap.tmp &&
	mv -f $bitmap.tmp $bitmap &&
	git rev-list --use-bitmap-index --count --all >actual 2>stderr &&
	test_cmp expect actual &&
	test_i18ngrep corrupt.ewah.bitmap stderr
'

test_expect_success 'truncated bitmap fails gracefully (cache)' '
	git repack -ad &&
	git rev-list --use-bitmap-index --count --all >expect &&
	bitmap=$(ls .git/objects/pack/*.bitmap) &&
	test_when_finished "rm -f $bitmap" &&
	test_copy_bytes 512 <$bitmap >$bitmap.tmp &&
	mv -f $bitmap.tmp $bitmap &&
	git rev-list --use-bitmap-index --count --all >actual 2>stderr &&
	test_cmp expect actual &&
	test_i18ngrep corrupted.bitmap.index stderr
'

test_expect_success 'enumerating progress counts pack-reused objects' '
	count=$(git rev-list --objects --all --count) &&
	git repack -adb &&

	# check first with only reused objects; confirm that our progress
	# showed the right number, and also that we did pack-reuse as expected.
	# Check only the final "done" line of the meter (there may be an
	# arbitrary number of intermediate lines ending with CR).
	GIT_PROGRESS_DELAY=0 \
		git pack-objects --all --stdout --progress \
		</dev/null >/dev/null 2>stderr &&
	grep "Enumerating objects: $count, done" stderr &&
	grep "pack-reused $count" stderr &&

	# now the same but with one non-reused object
	git commit --allow-empty -m "an extra commit object" &&
	GIT_PROGRESS_DELAY=0 \
		git pack-objects --all --stdout --progress \
		</dev/null >/dev/null 2>stderr &&
	grep "Enumerating objects: $((count+1)), done" stderr &&
	grep "pack-reused $count" stderr
'

# have_delta <obj> <expected_base>
#
# Note that because this relies on cat-file, it might find _any_ copy of an
# object in the repository. The caller is responsible for making sure
# there's only one (e.g., via "repack -ad", or having just fetched a copy).
have_delta () {
	echo $2 >expect &&
	echo $1 | git cat-file --batch-check="%(deltabase)" >actual &&
	test_cmp expect actual
}

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

test_done
