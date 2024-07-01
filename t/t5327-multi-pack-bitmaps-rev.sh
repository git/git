#!/bin/sh

test_description='exercise basic multi-pack bitmap functionality (.rev files)'

. ./test-lib.sh
. "${TEST_DIRECTORY}/lib-bitmap.sh"

# We'll be writing our own MIDX, so avoid getting confused by the automatic
# ones.
GIT_TEST_MULTI_PACK_INDEX=0
GIT_TEST_MULTI_PACK_INDEX_WRITE_INCREMENTAL=0

# Unlike t5326, this test exercise multi-pack bitmap functionality where the
# object order is stored in a separate .rev file.
GIT_TEST_MIDX_WRITE_REV=1
GIT_TEST_MIDX_READ_RIDX=0
export GIT_TEST_MIDX_WRITE_REV
export GIT_TEST_MIDX_READ_RIDX

test_midx_bitmap_rev () {
	writeLookupTable=false

	for i in "$@"
	do
		case $i in
		"pack.writeBitmapLookupTable") writeLookupTable=true;;
		esac
	done

	test_expect_success 'setup bitmap config' '
		rm -rf * .git &&
		git init &&
		git config pack.writeBitmapLookupTable '"$writeLookupTable"'
	'

	midx_bitmap_core rev
	midx_bitmap_partial_tests rev
}

test_midx_bitmap_rev
test_midx_bitmap_rev "pack.writeBitmapLookupTable"

test_done
