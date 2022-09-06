# Helpers for scripts testing bitmap functionality; see t5310 for
# example usage.

objdir=.git/objects
midx=$objdir/pack/multi-pack-index

# Compare a file containing rev-list bitmap traversal output to its non-bitmap
# counterpart. You can't just use test_cmp for this, because the two produce
# subtly different output:
#
#   - regular output is in traversal order, whereas bitmap is split by type,
#     with non-packed objects at the end
#
#   - regular output has a space and the pathname appended to non-commit
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
setup_bitmap_history() {
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
		git tag tagged-blob $blob
	'
}

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

basic_bitmap_tests () {
	tip="$1"
	test_expect_success 'rev-list --test-bitmap verifies bitmaps' "
		git rev-list --test-bitmap "${tip:-HEAD}"
	"

	rev_list_tests 'full bitmap'

	test_expect_success 'clone from bitmapped repository' '
		rm -fr clone.git &&
		git clone --no-local --bare . clone.git &&
		git rev-parse HEAD >expect &&
		git --git-dir=clone.git rev-parse HEAD >actual &&
		test_cmp expect actual
	'

	test_expect_success 'partial clone from bitmapped repository' '
		test_config uploadpack.allowfilter true &&
		rm -fr partial-clone.git &&
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

	test_expect_success 'enumerating progress counts pack-reused objects' '
		count=$(git rev-list --objects --all --count) &&
		git repack -adb &&

		# check first with only reused objects; confirm that our
		# progress showed the right number, and also that we did
		# pack-reuse as expected.  Check only the final "done"
		# line of the meter (there may be an arbitrary number of
		# intermediate lines ending with CR).
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
}

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

midx_checksum () {
	test-tool read-midx --checksum "$1"
}

# midx_pack_source <obj>
midx_pack_source () {
	test-tool read-midx --show-objects .git/objects | grep "^$1 " | cut -f2
}

test_rev_exists () {
	commit="$1"
	kind="$2"

	test_expect_success "reverse index exists ($kind)" '
		GIT_TRACE2_EVENT=$(pwd)/event.trace \
			git rev-list --test-bitmap "$commit" &&

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
		git repack -ad &&
		git multi-pack-index write --bitmap &&
		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap
	'

	test_rev_exists HEAD "$rev_kind"

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
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap
	'

	test_rev_exists HEAD "$rev_kind"

	basic_bitmap_tests

	test_expect_success '--no-bitmap is respected when bitmaps exist' '
		git multi-pack-index write --bitmap &&

		test_commit respect--no-bitmap &&
		git repack -d &&

		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&

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

	test_expect_success 'changing the preferred pack does not corrupt bitmaps' '
		rm -fr repo &&
		git init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&

			test_commit A &&
			test_commit B &&

			git rev-list --objects --no-object-names HEAD^ >A.objects &&
			git rev-list --objects --no-object-names HEAD^.. >B.objects &&

			A=$(git pack-objects $objdir/pack/pack <A.objects) &&
			B=$(git pack-objects $objdir/pack/pack <B.objects) &&

			cat >indexes <<-EOF &&
			pack-$A.idx
			pack-$B.idx
			EOF

			git multi-pack-index write --bitmap --stdin-packs \
				--preferred-pack=pack-$A.pack <indexes &&
			git rev-list --test-bitmap A &&

			git multi-pack-index write --bitmap --stdin-packs \
				--preferred-pack=pack-$B.pack <indexes &&
			git rev-list --test-bitmap A
		)
	'
}

midx_bitmap_partial_tests () {
	rev_kind="${1:-midx}"

	test_expect_success 'setup partial bitmaps' '
		test_commit packed &&
		git repack &&
		test_commit loose &&
		git multi-pack-index write --bitmap &&
		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap
	'

	test_rev_exists HEAD~ "$rev_kind"

	basic_bitmap_tests HEAD~
}
