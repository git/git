#!/bin/sh

test_description='exercise basic multi-pack bitmap functionality (.rev files)'

. ./test-lib.sh
. "${TEST_DIRECTORY}/lib-bitmap.sh"

# We'll be writing our own midx and bitmaps, so avoid getting confused by the
# automatic ones.
BUT_TEST_MULTI_PACK_INDEX=0
BUT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0

# Unlike t5326, this test exercise multi-pack bitmap functionality where the
# object order is stored in a separate .rev file.
BUT_TEST_MIDX_WRITE_REV=1
BUT_TEST_MIDX_READ_RIDX=0
export BUT_TEST_MIDX_WRITE_REV
export BUT_TEST_MIDX_READ_RIDX

midx_bitmap_core rev
midx_bitmap_partial_tests rev

test_done
