#!/bin/sh

test_description='incremental multi-pack-index'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-midx.sh

GIT_TEST_MULTI_PACK_INDEX=0
export GIT_TEST_MULTI_PACK_INDEX

objdir=.git/objects
packdir=$objdir/pack
midxdir=$packdir/multi-pack-index.d
midx_chain=$midxdir/multi-pack-index-chain

test_expect_success 'convert non-incremental MIDX to incremental' '
	test_commit base &&
	git repack -ad &&
	git multi-pack-index write &&

	test_path_is_file $packdir/multi-pack-index &&
	old_hash="$(midx_checksum $objdir)" &&

	test_commit other &&
	git repack -d &&
	git multi-pack-index write --incremental &&

	test_path_is_missing $packdir/multi-pack-index &&
	test_path_is_file $midx_chain &&
	test_line_count = 2 $midx_chain &&
	grep $old_hash $midx_chain
'

compare_results_with_midx 'incremental MIDX'

test_expect_success 'convert incremental to non-incremental' '
	test_commit squash &&
	git repack -d &&
	git multi-pack-index write &&

	test_path_is_file $packdir/multi-pack-index &&
	test_dir_is_empty $midxdir
'

compare_results_with_midx 'non-incremental MIDX conversion'

write_midx_layer () {
	n=1
	if test -f $midx_chain
	then
		n="$(($(wc -l <$midx_chain) + 1))"
	fi

	for i in 1 2
	do
		test_commit $n.$i &&
		git repack -d || return 1
	done &&
	git multi-pack-index write --bitmap --incremental
}

test_expect_success 'write initial MIDX layer' '
	git repack -ad &&
	write_midx_layer
'

test_expect_success 'read bitmap from first MIDX layer' '
	git rev-list --test-bitmap 1.2
'

test_expect_success 'write another MIDX layer' '
	write_midx_layer
'

test_expect_success 'midx verify with multiple layers' '
	test_path_is_file "$midx_chain" &&
	test_line_count = 2 "$midx_chain" &&

	git multi-pack-index verify
'

test_expect_success 'read bitmap from second MIDX layer' '
	git rev-list --test-bitmap 2.2
'

test_expect_success 'read earlier bitmap from second MIDX layer' '
	git rev-list --test-bitmap 1.2
'

test_expect_success 'show object from first pack' '
	git cat-file -p 1.1
'

test_expect_success 'show object from second pack' '
	git cat-file -p 2.2
'

for reuse in false single multi
do
	test_expect_success "full clone (pack.allowPackReuse=$reuse)" '
		rm -fr clone.git &&

		git config pack.allowPackReuse $reuse &&
		git clone --no-local --bare . clone.git
	'
done

test_expect_success 'relink existing MIDX layer' '
	rm -fr "$midxdir" &&

	GIT_TEST_MIDX_WRITE_REV=1 git multi-pack-index write --bitmap &&

	midx_hash="$(test-tool read-midx --checksum $objdir)" &&

	test_path_is_file "$packdir/multi-pack-index" &&
	test_path_is_file "$packdir/multi-pack-index-$midx_hash.bitmap" &&
	test_path_is_file "$packdir/multi-pack-index-$midx_hash.rev" &&

	test_commit another &&
	git repack -d &&
	git multi-pack-index write --bitmap --incremental &&

	test_path_is_missing "$packdir/multi-pack-index" &&
	test_path_is_missing "$packdir/multi-pack-index-$midx_hash.bitmap" &&
	test_path_is_missing "$packdir/multi-pack-index-$midx_hash.rev" &&

	test_path_is_file "$midxdir/multi-pack-index-$midx_hash.midx" &&
	test_path_is_file "$midxdir/multi-pack-index-$midx_hash.bitmap" &&
	test_path_is_file "$midxdir/multi-pack-index-$midx_hash.rev" &&
	test_line_count = 2 "$midx_chain"

'

test_done
