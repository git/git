#!/bin/sh

test_description='rev-list combining bitmaps and filters'
. ./test-lib.sh

test_expect_success 'set up bitmapped repo' '
	# one commit will have bitmaps, the other will not
	test_commit one &&
	test_commit much-larger-blob-one &&
	git repack -adb &&
	test_commit two &&
	test_commit much-larger-blob-two
'

test_expect_success 'filters fallback to non-bitmap traversal' '
	# use a path-based filter, since they are inherently incompatible with
	# bitmaps (i.e., this test will never get confused by later code to
	# combine the features)
	filter=$(echo "!one" | git hash-object -w --stdin) &&
	git rev-list --objects --filter=sparse:oid=$filter HEAD >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter=sparse:oid=$filter HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'blob:none filter' '
	git rev-list --objects --filter=blob:none HEAD >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter=blob:none HEAD >actual &&
	test_bitmap_traversal expect actual
'

test_expect_success 'blob:none filter with specified blob' '
	git rev-list --objects --filter=blob:none HEAD HEAD:two.t >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter=blob:none HEAD HEAD:two.t >actual &&
	test_bitmap_traversal expect actual
'

test_expect_success 'blob:limit filter' '
	git rev-list --objects --filter=blob:limit=5 HEAD >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter=blob:limit=5 HEAD >actual &&
	test_bitmap_traversal expect actual
'

test_expect_success 'blob:limit filter with specified blob' '
	git rev-list --objects --filter=blob:limit=5 \
		     HEAD HEAD:much-larger-blob-two.t >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter=blob:limit=5 \
		     HEAD HEAD:much-larger-blob-two.t >actual &&
	test_bitmap_traversal expect actual
'

test_done
