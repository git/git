#!/bin/sh

test_description='multi-pack-index compaction'

. ./test-lib.sh

GIT_TEST_MULTI_PACK_INDEX=0
GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0
GIT_TEST_MULTI_PACK_INDEX_WRITE_INCREMENTAL=0

objdir=.git/objects
packdir=$objdir/pack
midxdir=$packdir/multi-pack-index.d
midx_chain=$midxdir/multi-pack-index-chain

nth_line() {
	local n="$1"
	shift
	awk "NR==$n" "$@"
}

write_packs () {
	for c in "$@"
	do
		test_commit "$c" &&

		git pack-objects --all --unpacked $packdir/pack-$c &&
		git prune-packed &&

		git multi-pack-index write --incremental --bitmap || return 1
	done
}

test_midx_layer_packs () {
	local checksum="$1" &&
	shift &&

	test-tool read-midx $objdir "$checksum" >out &&

	printf "%s\n" "$@" >expect &&
	# NOTE: do *not* pipe through sort here, we want to ensure the
	# order of packs is preserved during compaction.
	grep "^pack-" out | cut -d"-" -f2 >actual &&

	test_cmp expect actual
}

test_midx_layer_object_uniqueness () {
	: >objs.all
	while read layer
	do
		test-tool read-midx --show-objects $objdir "$layer" >out &&
		grep "\.pack$" out | cut -d" " -f1 | sort >objs.layer &&
		test_stdout_line_count = 0 comm -12 objs.all objs.layer &&
		cat objs.all objs.layer | sort >objs.tmp &&
		mv objs.tmp objs.all || return 1
	done <$midx_chain
}

test_expect_success 'MIDX compaction with lex-ordered pack names' '
	git init midx-compact-lex-order &&
	(
		cd midx-compact-lex-order &&

		git config maintenance.auto false &&

		write_packs A B C D E &&
		test_line_count = 5 $midx_chain &&

		git multi-pack-index compact --incremental --bitmap \
			"$(nth_line 2 "$midx_chain")" \
			"$(nth_line 4 "$midx_chain")" &&
		test_line_count = 3 $midx_chain &&

		test_midx_layer_packs "$(nth_line 1 "$midx_chain")" A &&
		test_midx_layer_packs "$(nth_line 2 "$midx_chain")" B C D &&
		test_midx_layer_packs "$(nth_line 3 "$midx_chain")" E &&

		test_midx_layer_object_uniqueness
	)
'

test_expect_success 'MIDX compaction with non-lex-ordered pack names' '
	git init midx-compact-non-lex-order &&
	(
		cd midx-compact-non-lex-order &&

		git config maintenance.auto false &&

		write_packs D C A B E &&
		test_line_count = 5 $midx_chain &&

		git multi-pack-index compact --incremental --bitmap \
			"$(nth_line 2 "$midx_chain")" \
			"$(nth_line 4 "$midx_chain")" &&
		test_line_count = 3 $midx_chain &&

		test_midx_layer_packs "$(nth_line 1 "$midx_chain")" D &&
		test_midx_layer_packs "$(nth_line 2 "$midx_chain")" C A B &&
		test_midx_layer_packs "$(nth_line 3 "$midx_chain")" E &&

		test_midx_layer_object_uniqueness
	)
'

test_expect_success 'setup for bogus MIDX compaction scenarios' '
	git init midx-compact-bogus &&
	(
		cd midx-compact-bogus &&

		git config maintenance.auto false &&

		write_packs A B C
	)
'

test_expect_success 'MIDX compaction with missing endpoints' '
	(
		cd midx-compact-bogus &&

		test_must_fail git multi-pack-index compact --incremental \
			"<missing>" "<missing>" 2>err &&
		test_grep "could not find MIDX: <missing>" err &&

		test_must_fail git multi-pack-index compact --incremental \
			"<missing>" "$(nth_line 2 "$midx_chain")" 2>err &&
		test_grep "could not find MIDX: <missing>" err &&

		test_must_fail git multi-pack-index compact --incremental \
			"$(nth_line 2 "$midx_chain")" "<missing>" 2>err &&
		test_grep "could not find MIDX: <missing>" err
	)
'

test_expect_success 'MIDX compaction with reversed endpoints' '
	(
		cd midx-compact-bogus &&

		from="$(nth_line 3 "$midx_chain")" &&
		to="$(nth_line 1 "$midx_chain")" &&

		test_must_fail git multi-pack-index compact --incremental \
			"$from" "$to" 2>err &&

		test_grep "MIDX $from must be an ancestor of $to" err
	)
'

test_expect_success 'MIDX compaction with identical endpoints' '
	(
		cd midx-compact-bogus &&

		from="$(nth_line 3 "$midx_chain")" &&
		to="$(nth_line 3 "$midx_chain")" &&

		test_must_fail git multi-pack-index compact --incremental \
			"$from" "$to" 2>err &&

		test_grep "MIDX compaction endpoints must be unique" err
	)
'

test_expect_success 'MIDX compaction with midx.version=1' '
	(
		cd midx-compact-bogus &&

		test_must_fail git -c midx.version=1 multi-pack-index compact \
			"$(nth_line 1 "$midx_chain")" \
			"$(nth_line 2 "$midx_chain")" 2>err &&

		test_grep "fatal: cannot perform MIDX compaction with v1 format" err
	)
'

midx_objs_by_pack () {
	awk '/\.pack$/ { split($3, a, "-"); print a[2], $1 }' | sort
}

tag_objs_from_pack () {
	objs="$(git rev-list --objects --no-object-names "$2")" &&
	printf "$1 %s\n" $objs | sort
}

test_expect_success 'MIDX compaction preserves pack object selection' '
	git init midx-compact-preserve-selection &&
	(
		cd midx-compact-preserve-selection &&

		git config maintenance.auto false &&

		test_commit A &&
		test_commit B &&

		# Create two packs, one containing just the objects from
		# A, and another containing all objects from the
		# repository.
		p1="$(echo A | git pack-objects --revs --delta-base-offset \
			$packdir/pack-1)" &&
		p0="$(echo B | git pack-objects --revs --delta-base-offset \
			$packdir/pack-0)" &&

		echo "pack-1-$p1.idx" | git multi-pack-index write \
			--incremental --bitmap --stdin-packs &&
		echo "pack-0-$p0.idx" | git multi-pack-index write \
			--incremental --bitmap --stdin-packs &&

		write_packs C &&

		git multi-pack-index compact --incremental --bitmap \
			"$(nth_line 1 "$midx_chain")" \
			"$(nth_line 2 "$midx_chain")" &&


		test-tool read-midx --show-objects $objdir \
			"$(nth_line 1 "$midx_chain")" >AB.info &&
		test-tool read-midx --show-objects $objdir \
			"$(nth_line 2 "$midx_chain")" >C.info &&

		midx_objs_by_pack <AB.info >AB.actual &&
		midx_objs_by_pack <C.info >C.actual &&

		{
			tag_objs_from_pack 1 A &&
			tag_objs_from_pack 0 A..B
		} | sort >AB.expect &&
		tag_objs_from_pack C B..C >C.expect &&

		test_cmp AB.expect AB.actual &&
		test_cmp C.expect C.actual
	)
'

test_expect_success 'MIDX compaction with bitmaps' '
	git init midx-compact-with-bitmaps &&
	(
		cd midx-compact-with-bitmaps &&

		git config maintenance.auto false &&

		write_packs foo bar baz quux woot &&

		test-tool read-midx --bitmap $objdir >bitmap.expect &&
		git multi-pack-index compact --incremental --bitmap \
			"$(nth_line 2 "$midx_chain")" \
			"$(nth_line 4 "$midx_chain")" &&
		test-tool read-midx --bitmap $objdir >bitmap.actual &&

		test_cmp bitmap.expect bitmap.actual &&

		true
	)
'

test_expect_success 'MIDX compaction with bitmaps (non-trivial)' '
	git init midx-compact-with-bitmaps-non-trivial &&
	(
		cd midx-compact-with-bitmaps-non-trivial &&

		git config maintenance.auto false &&

		git branch -m main &&

		#               D(4)
		#              /
		# A(1) --- B(2) --- C(3) --- G(7)
		#              \
		#               E(5) --- F(6)
		write_packs A B C &&
		git checkout -b side &&
		write_packs D &&
		git checkout -b other B &&
		write_packs E F &&
		git checkout main &&
		write_packs G &&

		# Compact layers 2-4, leaving us with:
		#
		#  [A, [B, C, D], E, F, G]
		git multi-pack-index compact --incremental --bitmap \
			"$(nth_line 2 "$midx_chain")" \
			"$(nth_line 4 "$midx_chain")" &&

		# Then compact the top two layers, condensing the above
		# such that the new 4th layer contains F and G.
		#
		#  [A, [B, C, D], E, [F, G]]
		git multi-pack-index compact --incremental --bitmap \
			"$(nth_line 4 "$midx_chain")" \
			"$(nth_line 5 "$midx_chain")"
	)
'

test_done
