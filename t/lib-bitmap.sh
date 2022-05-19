# Helpers for scripts testing bitmap functionality; see t5310 for
# example usage.

objdir=.but/objects
midx=$objdir/pack/multi-pack-index

# Compare a file containing rev-list bitmap traversal output to its non-bitmap
# counterpart. You can't just use test_cmp for this, because the two produce
# subtly different output:
#
#   - regular output is in traversal order, whereas bitmap is split by type,
#     with non-packed objects at the end
#
#   - regular output has a space and the pathname appended to non-cummit
#     objects; bitmap output omits this
#
# This function normalizes and compares the two. The second file should
# always be the bitmap output.
test_bitmap_traversal () {
	if test "$1" = "--no-confirm-bitmaps"
	then
		shift
	elif cmp "$1" "$2"
	then
		echo >&2 "identical raw outputs; are you sure bitmaps were used?"
		return 1
	fi &&
	cut -d' ' -f1 "$1" | sort >"$1.normalized" &&
	sort "$2" >"$2.normalized" &&
	test_cmp "$1.normalized" "$2.normalized" &&
	rm -f "$1.normalized" "$2.normalized"
}

# To ensure the logic for "maximal cummits" is exercised, make
# the repository a bit more complicated.
#
#    other                         second
#      *                             *
# (99 cummits)                  (99 cummits)
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
# makes some of these cummits unimportant!
#
# The important part for the maximal cummit algorithm is how
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
# cummit multiple times. That goal might be important
# again, so preserve this complicated case. For now, this
# test will guarantee that the bitmaps are computed
# correctly, even with the repeat calculations.
setup_bitmap_history() {
	test_expect_success 'setup repo with moderate-sized history' '
		test_cummit_bulk --id=file 10 &&
		but branch -M second &&
		but checkout -b other HEAD~5 &&
		test_cummit_bulk --id=side 10 &&

		# add complicated history setup, including merges and
		# ambiguous merge-bases

		but checkout -b merge-left other~2 &&
		but merge second~2 -m "merge-left" &&

		but checkout -b merge-right second~1 &&
		but merge other~1 -m "merge-right" &&

		but checkout -b octo-second second &&
		but merge merge-left merge-right -m "octopus-second" &&

		but checkout -b octo-other other &&
		but merge merge-left merge-right -m "octopus-other" &&

		but checkout other &&
		but merge octo-other -m "pull octopus" &&

		but checkout second &&
		but merge octo-second -m "pull octopus" &&

		# Remove these branches so they are not selected
		# as bitmap tips
		but branch -D merge-left &&
		but branch -D merge-right &&
		but branch -D octo-other &&
		but branch -D octo-second &&

		# add padding to make these merges less interesting
		# and avoid having them selected for bitmaps
		test_cummit_bulk --id=file 100 &&
		but checkout other &&
		test_cummit_bulk --id=side 100 &&
		but checkout second &&

		bitmaptip=$(but rev-parse second) &&
		blob=$(echo tagged-blob | but hash-object -w --stdin) &&
		but tag tagged-blob $blob
	'
}

rev_list_tests_head () {
	test_expect_success "counting cummits via bitmap ($state, $branch)" '
		but rev-list --count $branch >expect &&
		but rev-list --use-bitmap-index --count $branch >actual &&
		test_cmp expect actual
	'

	test_expect_success "counting partial cummits via bitmap ($state, $branch)" '
		but rev-list --count $branch~5..$branch >expect &&
		but rev-list --use-bitmap-index --count $branch~5..$branch >actual &&
		test_cmp expect actual
	'

	test_expect_success "counting cummits with limit ($state, $branch)" '
		but rev-list --count -n 1 $branch >expect &&
		but rev-list --use-bitmap-index --count -n 1 $branch >actual &&
		test_cmp expect actual
	'

	test_expect_success "counting non-linear history ($state, $branch)" '
		but rev-list --count other...second >expect &&
		but rev-list --use-bitmap-index --count other...second >actual &&
		test_cmp expect actual
	'

	test_expect_success "counting cummits with limiting ($state, $branch)" '
		but rev-list --count $branch -- 1.t >expect &&
		but rev-list --use-bitmap-index --count $branch -- 1.t >actual &&
		test_cmp expect actual
	'

	test_expect_success "counting objects via bitmap ($state, $branch)" '
		but rev-list --count --objects $branch >expect &&
		but rev-list --use-bitmap-index --count --objects $branch >actual &&
		test_cmp expect actual
	'

	test_expect_success "enumerate cummits ($state, $branch)" '
		but rev-list --use-bitmap-index $branch >actual &&
		but rev-list $branch >expect &&
		test_bitmap_traversal --no-confirm-bitmaps expect actual
	'

	test_expect_success "enumerate --objects ($state, $branch)" '
		but rev-list --objects --use-bitmap-index $branch >actual &&
		but rev-list --objects $branch >expect &&
		test_bitmap_traversal expect actual
	'

	test_expect_success "bitmap --objects handles non-cummit objects ($state, $branch)" '
		but rev-list --objects --use-bitmap-index $branch tagged-blob >actual &&
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

basic_bitmap_tests () {
	tip="$1"
	test_expect_success 'rev-list --test-bitmap verifies bitmaps' "
		but rev-list --test-bitmap "${tip:-HEAD}"
	"

	rev_list_tests 'full bitmap'

	test_expect_success 'clone from bitmapped repository' '
		rm -fr clone.but &&
		but clone --no-local --bare . clone.but &&
		but rev-parse HEAD >expect &&
		but --but-dir=clone.but rev-parse HEAD >actual &&
		test_cmp expect actual
	'

	test_expect_success 'partial clone from bitmapped repository' '
		test_config uploadpack.allowfilter true &&
		rm -fr partial-clone.but &&
		but clone --no-local --bare --filter=blob:none . partial-clone.but &&
		(
			cd partial-clone.but &&
			pack=$(echo objects/pack/*.pack) &&
			but verify-pack -v "$pack" >have &&
			awk "/blob/ { print \$1 }" <have >blobs &&
			# we expect this single blob because of the direct ref
			but rev-parse refs/tags/tagged-blob >expect &&
			test_cmp expect blobs
		)
	'

	test_expect_success 'setup further non-bitmapped cummits' '
		test_cummit_bulk --id=further 10
	'

	rev_list_tests 'partial bitmap'

	test_expect_success 'fetch (partial bitmap)' '
		but --but-dir=clone.but fetch origin second:second &&
		but rev-parse HEAD >expect &&
		but --but-dir=clone.but rev-parse HEAD >actual &&
		test_cmp expect actual
	'

	test_expect_success 'enumerating progress counts pack-reused objects' '
		count=$(but rev-list --objects --all --count) &&
		but repack -adb &&

		# check first with only reused objects; confirm that our
		# progress showed the right number, and also that we did
		# pack-reuse as expected.  Check only the final "done"
		# line of the meter (there may be an arbitrary number of
		# intermediate lines ending with CR).
		GIT_PROGRESS_DELAY=0 \
			but pack-objects --all --stdout --progress \
			</dev/null >/dev/null 2>stderr &&
		grep "Enumerating objects: $count, done" stderr &&
		grep "pack-reused $count" stderr &&

		# now the same but with one non-reused object
		but cummit --allow-empty -m "an extra cummit object" &&
		GIT_PROGRESS_DELAY=0 \
			but pack-objects --all --stdout --progress \
			</dev/null >/dev/null 2>stderr &&
		grep "Enumerating objects: $((count+1)), done" stderr &&
		grep "pack-reused $count" stderr
	'
}

# have_delta <obj> <expected_base>
#
# Note that because this relies on cat-file, it might find _any_ copy of an
# object in the repository. The caller is responsible for making sure
# there's only one (e.g., via "repack -ad", or having just fetched a copy).
have_delta () {
	echo $2 >expect &&
	echo $1 | but cat-file --batch-check="%(deltabase)" >actual &&
	test_cmp expect actual
}

midx_checksum () {
	test-tool read-midx --checksum "$1"
}

# midx_pack_source <obj>
midx_pack_source () {
	test-tool read-midx --show-objects .but/objects | grep "^$1 " | cut -f2
}

test_rev_exists () {
	cummit="$1"
	kind="$2"

	test_expect_success "reverse index exists ($kind)" '
		GIT_TRACE2_EVENT=$(pwd)/event.trace \
			but rev-list --test-bitmap "$cummit" &&

		if test "rev" = "$kind"
		then
			test_path_is_file $midx-$(midx_checksum $objdir).rev
		fi &&
		grep "\"category\":\"load_midx_revindex\",\"key\":\"source\",\"value\":\"$kind\"" event.trace
	'
}

midx_bitmap_core () {
	rev_kind="${1:-midx}"

	setup_bitmap_history

	test_expect_success 'create single-pack midx with bitmaps' '
		but repack -ad &&
		but multi-pack-index write --bitmap &&
		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap
	'

	test_rev_exists HEAD "$rev_kind"

	basic_bitmap_tests

	test_expect_success 'create new additional packs' '
		for i in $(test_seq 1 16)
		do
			test_cummit "$i" &&
			but repack -d || return 1
		done &&

		but checkout -b other2 HEAD~8 &&
		for i in $(test_seq 1 8)
		do
			test_cummit "side-$i" &&
			but repack -d || return 1
		done &&
		but checkout second
	'

	test_expect_success 'create multi-pack midx with bitmaps' '
		but multi-pack-index write --bitmap &&

		ls $objdir/pack/pack-*.pack >packs &&
		test_line_count = 25 packs &&

		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap
	'

	test_rev_exists HEAD "$rev_kind"

	basic_bitmap_tests

	test_expect_success '--no-bitmap is respected when bitmaps exist' '
		but multi-pack-index write --bitmap &&

		test_cummit respect--no-bitmap &&
		but repack -d &&

		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&

		but multi-pack-index write --no-bitmap &&

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

		but add a b &&
		but cummit -m "initial cummit" &&

		a=$(but rev-parse HEAD:a) &&
		b=$(but rev-parse HEAD:b) &&

		# In the first pack, "a" is stored as a delta to "b".
		p1=$(but pack-objects .but/objects/pack/pack <<-EOF
		$a
		$b
		EOF
		) &&

		# In the second pack, "a" is missing, and "b" is not a delta nor base to
		# any other object.
		p2=$(but pack-objects .but/objects/pack/pack <<-EOF
		$b
		$(but rev-parse HEAD)
		$(but rev-parse HEAD^{tree})
		EOF
		) &&

		but prune-packed &&
		# Use the second pack as the preferred source, so that "b" occurs
		# earlier in the MIDX object order, rendering "a" unusable for pack
		# reuse.
		but multi-pack-index write --bitmap --preferred-pack=pack-$p2.idx &&

		have_delta $a $b &&
		test $(midx_pack_source $a) != $(midx_pack_source $b)
	'

	rev_list_tests 'full bitmap with backwards delta'

	test_expect_success 'clone with bitmaps enabled' '
		but clone --no-local --bare . clone-reverse-delta.but &&
		test_when_finished "rm -fr clone-reverse-delta.but" &&

		but rev-parse HEAD >expect &&
		but --but-dir=clone-reverse-delta.but rev-parse HEAD >actual &&
		test_cmp expect actual
	'

	test_expect_success 'changing the preferred pack does not corrupt bitmaps' '
		rm -fr repo &&
		but init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&

			test_cummit A &&
			test_cummit B &&

			but rev-list --objects --no-object-names HEAD^ >A.objects &&
			but rev-list --objects --no-object-names HEAD^.. >B.objects &&

			A=$(but pack-objects $objdir/pack/pack <A.objects) &&
			B=$(but pack-objects $objdir/pack/pack <B.objects) &&

			cat >indexes <<-EOF &&
			pack-$A.idx
			pack-$B.idx
			EOF

			but multi-pack-index write --bitmap --stdin-packs \
				--preferred-pack=pack-$A.pack <indexes &&
			but rev-list --test-bitmap A &&

			but multi-pack-index write --bitmap --stdin-packs \
				--preferred-pack=pack-$B.pack <indexes &&
			but rev-list --test-bitmap A
		)
	'
}

midx_bitmap_partial_tests () {
	rev_kind="${1:-midx}"

	test_expect_success 'setup partial bitmaps' '
		test_cummit packed &&
		but repack &&
		test_cummit loose &&
		but multi-pack-index write --bitmap 2>err &&
		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap
	'

	test_rev_exists HEAD~ "$rev_kind"

	basic_bitmap_tests HEAD~
}
