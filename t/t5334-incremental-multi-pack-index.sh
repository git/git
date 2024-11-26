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

test_done
