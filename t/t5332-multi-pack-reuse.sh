#!/bin/sh

test_description='pack-objects multi-pack reuse'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-bitmap.sh
. "$TEST_DIRECTORY"/lib-pack.sh

GIT_TEST_MULTI_PACK_INDEX=0
GIT_TEST_MULTI_PACK_INDEX_WRITE_INCREMENTAL=0

# The --path-walk option does not consider the preferred pack
# at all for reusing deltas, so this variable changes the
# behavior of this test, if enabled.
GIT_TEST_PACK_PATH_WALK=0
export GIT_TEST_PACK_PATH_WALK

objdir=.git/objects
packdir=$objdir/pack

test_pack_reused () {
	test_trace2_data pack-objects pack-reused "$1"
}

test_packs_reused () {
	test_trace2_data pack-objects packs-reused "$1"
}


# pack_position <object> </path/to/pack.idx
pack_position () {
	git show-index >objects &&
	grep "$1" objects | cut -d" " -f1
}

# B as an OFS_DELTA against A at the given one-byte distance.
pack_obj_b_ofs_a () {
	pack_obj "$B" "$A" >b-ref.tmp &&
	printf "\145" &&
	printf "\\$(printf "%03o" "$1")" &&
	dd if=b-ref.tmp bs=1 skip=$((1 + $(test_oid rawsz))) 2>/dev/null
}

# test_pack_objects_reused_all <pack-reused> <packs-reused>
test_pack_objects_reused_all () {
	: >trace2.txt &&
	GIT_TRACE2_EVENT="$PWD/trace2.txt" \
		git pack-objects --stdout --revs --all --delta-base-offset \
		>got.pack &&

	test_pack_reused "$1" <trace2.txt &&
	test_packs_reused "$2" <trace2.txt &&

	git index-pack --strict -o got.idx got.pack
}

# test_pack_objects_reused <pack-reused> <packs-reused>
test_pack_objects_reused () {
	: >trace2.txt &&
	GIT_TRACE2_EVENT="$PWD/trace2.txt" \
		git pack-objects --stdout --revs >got.pack &&

	test_pack_reused "$1" <trace2.txt &&
	test_packs_reused "$2" <trace2.txt &&

	git index-pack --strict -o got.idx got.pack
}

test_expect_success 'preferred pack is reused for single-pack reuse' '
	test_config pack.allowPackReuse single &&
	git config set maintenance.auto false &&

	for i in A B
	do
		test_commit "$i" &&
		git repack -d || return 1
	done &&

	git multi-pack-index write --bitmap &&

	test_pack_objects_reused_all 3 1
'

test_expect_success 'multi-pack reuse is disabled by default' '
	test_pack_objects_reused_all 3 1
'

test_expect_success 'feature.experimental implies multi-pack reuse' '
	test_config feature.experimental true &&

	test_pack_objects_reused_all 6 2
'

test_expect_success 'multi-pack reuse can be disabled with feature.experimental' '
	test_config feature.experimental true &&
	test_config pack.allowPackReuse single &&

	test_pack_objects_reused_all 3 1
'

test_expect_success 'enable multi-pack reuse' '
	git config pack.allowPackReuse multi
'

test_expect_success 'reuse all objects from subset of bitmapped packs' '
	test_commit C &&
	git repack -d &&

	git multi-pack-index write --bitmap &&

	cat >in <<-EOF &&
	$(git rev-parse C)
	^$(git rev-parse A)
	EOF

	test_pack_objects_reused 6 2 <in
'

test_expect_success 'reuse all objects from all packs' '
	test_pack_objects_reused_all 9 3
'

test_expect_success 'reuse objects from first pack with middle gap' '
	for i in D E F
	do
		test_commit "$i" || return 1
	done &&

	# Set "pack.window" to zero to ensure that we do not create any
	# deltas, which could alter the amount of pack reuse we perform
	# (if, for e.g., we are not sending one or more bases).
	D="$(git -c pack.window=0 pack-objects --all --unpacked $packdir/pack)" &&

	d_pos="$(pack_position $(git rev-parse D) <$packdir/pack-$D.idx)" &&
	e_pos="$(pack_position $(git rev-parse E) <$packdir/pack-$D.idx)" &&
	f_pos="$(pack_position $(git rev-parse F) <$packdir/pack-$D.idx)" &&

	# commits F, E, and D, should appear in that order at the
	# beginning of the pack
	test $f_pos -lt $e_pos &&
	test $e_pos -lt $d_pos &&

	# Ensure that the pack we are constructing sorts ahead of any
	# other packs in lexical/bitmap order by choosing it as the
	# preferred pack.
	git multi-pack-index write --bitmap --preferred-pack="pack-$D.idx" &&

	cat >in <<-EOF &&
	$(git rev-parse E)
	^$(git rev-parse D)
	EOF

	test_pack_objects_reused 3 1 <in
'

test_expect_success 'reuse objects from middle pack with middle gap' '
	rm -fr $packdir/multi-pack-index* &&

	# Ensure that the pack we are constructing sort into any
	# position *but* the first one, by choosing a different pack as
	# the preferred one.
	git multi-pack-index write --bitmap --preferred-pack="pack-$A.idx" &&

	cat >in <<-EOF &&
	$(git rev-parse E)
	^$(git rev-parse D)
	EOF

	test_pack_objects_reused 3 1 <in
'

test_expect_success 'omit delta with uninteresting base (same pack)' '
	git repack -adk &&

	test_seq 32 >f &&
	git add f &&
	test_tick &&
	git commit -m "delta" &&
	delta="$(git rev-parse HEAD)" &&

	test_seq 64 >f &&
	test_tick &&
	git commit -a -m "base" &&
	base="$(git rev-parse HEAD)" &&

	test_commit other &&

	git repack -d &&

	have_delta "$(git rev-parse $delta:f)" "$(git rev-parse $base:f)" &&

	git multi-pack-index write --bitmap &&

	cat >in <<-EOF &&
	$(git rev-parse other)
	^$base
	EOF

	# We can only reuse the 3 objects corresponding to "other" from
	# the latest pack.
	#
	# This is because even though we want "delta", we do not want
	# "base", meaning that we have to inflate the delta/base-pair
	# corresponding to the blob in commit "delta", which bypasses
	# the pack-reuse mechanism.
	#
	# The remaining objects from the other pack are similarly not
	# reused because their objects are on the uninteresting side of
	# the query.
	test_pack_objects_reused 3 1 <in
'

test_expect_success 'omit delta from uninteresting base (cross pack)' '
	cat >in <<-EOF &&
	$(git rev-parse $base)
	^$(git rev-parse $delta)
	EOF

	P="$(git pack-objects --revs $packdir/pack <in)" &&

	git multi-pack-index write --bitmap --preferred-pack="pack-$P.idx" &&

	packs_nr="$(find $packdir -type f -name "pack-*.pack" | wc -l)" &&
	objects_nr="$(git rev-list --count --all --objects)" &&

	test_pack_objects_reused_all $(($objects_nr - 1)) $packs_nr
'

test_expect_success 'non-omitted delta in MIDX preferred pack' '
	test_config pack.allowPackReuse single &&

	cat >p1.objects <<-EOF &&
	$(git rev-parse $base)
	^$(git rev-parse $delta^)
	EOF
	cat >p2.objects <<-EOF &&
	$(git rev-parse F)
	EOF

	p1="$(git pack-objects --revs $packdir/pack <p1.objects)" &&
	p2="$(git pack-objects --revs $packdir/pack <p2.objects)" &&

	cat >in <<-EOF &&
	pack-$p1.idx
	pack-$p2.idx
	EOF
	git multi-pack-index write --bitmap --stdin-packs \
		--preferred-pack=pack-$p1.pack <in &&

	git show-index <$packdir/pack-$p1.idx >expect &&

	test_pack_objects_reused_all $(wc -l <expect) 1
'

test_expect_success 'duplicate objects' '
	git init duplicate-objects &&
	(
		cd duplicate-objects &&

		git config pack.allowPackReuse multi &&

		test_commit base &&

		git repack -a &&

		git rev-parse HEAD^{tree} >in &&
		p="$(git pack-objects $packdir/pack <in)" &&

		git multi-pack-index write --bitmap --preferred-pack=pack-$p.idx &&

		objects_nr="$(git rev-list --count --all --objects)" &&
		packs_nr="$(find $packdir -type f -name "pack-*.pack" | wc -l)" &&

		test_pack_objects_reused_all $objects_nr $packs_nr
	)
'

test_expect_success 'duplicate objects with verbatim reuse' '
	git init duplicate-objects-verbatim &&
	(
		cd duplicate-objects-verbatim &&

		git config pack.allowPackReuse multi &&

		test_commit_bulk 64 &&

		# take the first object from the main pack...
		git show-index <$(ls $packdir/pack-*.idx) >obj.raw &&
		sort -nk1 <obj.raw | head -n1 | cut -d" " -f2 >in &&

		# ...and create a separate pack containing just that object
		p="$(git pack-objects $packdir/pack <in)" &&

		git multi-pack-index write --bitmap --preferred-pack=pack-$p.idx &&

		test_pack_objects_reused_all 192 2
	)
'

test_expect_success 'reuse with intra-pack duplicate objects' '
	git init intra-pack-duplicate-objects &&
	(
		cd intra-pack-duplicate-objects &&

		# Make enough objects to exercise whole-word reuse.
		test_commit_bulk 20 &&
		test_commit --printf A a "\7\0" &&
		test_commit --printf B b "\7\76" &&

		objects_nr=$(git rev-list --count --objects --all) &&
		git rev-list --objects --all |
		cut -d" " -f1 >objects &&
		A=$(test_oid packlib_7_0) &&
		B=$(test_oid packlib_7_76) &&
		grep -v -e "^$A$" -e "^$B$" objects >rest &&
		pack_obj "$A" >a-full &&
		pack_obj "$B" >b-full &&
		while read oid
		do
			pack_obj "$oid" || exit 1
		done <rest >rest.entries &&
		{
			# Arrange the pack as A, B, A, C..., so that physical
			# positions diverge from MIDX pseudo-pack order.
			pack_header $((objects_nr + 1)) &&
			cat a-full b-full a-full rest.entries
		} >duplicate.pack &&
		pack_trailer duplicate.pack &&
		clear_packs &&
		git index-pack --stdin <duplicate.pack &&

		git multi-pack-index write --bitmap &&
		git config pack.allowPackReuse single &&
		test_pack_objects_reused_all "$objects_nr" 1 &&
		rm -f got.idx &&
		test_env GIT_TEST_MIDX_READ_BTMP=false \
			test_pack_objects_reused_all 0 0
	)
'

test_expect_success 'omit delta whose duplicate base is not selected' '
	(
		cd intra-pack-duplicate-objects &&

		A=$(test_oid packlib_7_0) &&
		B=$(test_oid packlib_7_76) &&
		objects_nr=$(wc -l <objects) &&

		a_size=$(wc -c <a-full) &&
		test "$((2 * a_size))" -lt 128 &&

		# The .idx order of duplicate OIDs is unspecified. Try a delta
		# against each copy and keep the pack whose base was omitted.
		for distance in "$((2 * a_size))" "$a_size"
		do
			base_offset=$((12 + 2 * a_size - distance)) &&
			pack_obj_b_ofs_a "$distance" >b-delta &&
			{
				pack_header "$((objects_nr + 1))" &&
				cat a-full a-full b-delta rest.entries
			} >candidate.pack &&
			pack_trailer candidate.pack &&
			clear_packs &&
			git index-pack --stdin <candidate.pack &&
			git multi-pack-index write --bitmap || return 1

			selected_offset=$(
				test-tool read-midx --show-objects "$objdir" |
				awk -v oid="$A" "\$1 == oid { print \$2 }"
			) || return 1
			test -n "$selected_offset" || return 1
			test "$selected_offset" = "$base_offset" || break
		done &&
		test "$selected_offset" != "$base_offset" &&

		test_pack_objects_reused_all "$((objects_nr - 1))" 1
	)
'

test_done
