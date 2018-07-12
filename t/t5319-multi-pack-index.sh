#!/bin/sh

test_description='multi-pack-indexes'
. ./test-lib.sh

test_expect_success 'write midx with no packs' '
	git multi-pack-index --object-dir=. write
'

test_done
