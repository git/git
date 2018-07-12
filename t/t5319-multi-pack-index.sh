#!/bin/sh

test_description='multi-pack-indexes'
. ./test-lib.sh

test_expect_success 'write midx with no packs' '
	test_when_finished rm -f pack/multi-pack-index &&
	git multi-pack-index --object-dir=. write &&
	test_path_is_file pack/multi-pack-index
'

test_done
