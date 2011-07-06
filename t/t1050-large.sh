#!/bin/sh
# Copyright (c) 2011, Google Inc.

test_description='adding and checking out large blobs'

. ./test-lib.sh

test_expect_success setup '
	git config core.bigfilethreshold 200k &&
	echo X | dd of=large bs=1k seek=2000
'

test_expect_success 'add a large file' '
	git add large &&
	# make sure we got a packfile and no loose objects
	test -f .git/objects/pack/pack-*.pack &&
	test ! -f .git/objects/??/??????????????????????????????????????
'

test_expect_success 'checkout a large file' '
	large=$(git rev-parse :large) &&
	git update-index --add --cacheinfo 100644 $large another &&
	git checkout another &&
	cmp large another ;# this must not be test_cmp
'

test_done
