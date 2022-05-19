#!/bin/sh

test_description='rev-list combining bitmaps and filters'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-bitmap.sh

test_expect_success 'set up bitmapped repo' '
	# one cummit will have bitmaps, the other will not
	test_cummit one &&
	test_cummit much-larger-blob-one &&
	but repack -adb &&
	test_cummit two &&
	test_cummit much-larger-blob-two &&
	but tag tag
'

test_expect_success 'filters fallback to non-bitmap traversal' '
	# use a path-based filter, since they are inherently incompatible with
	# bitmaps (i.e., this test will never get confused by later code to
	# combine the features)
	filter=$(echo "!one" | but hash-object -w --stdin) &&
	but rev-list --objects --filter=sparse:oid=$filter HEAD >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter=sparse:oid=$filter HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'blob:none filter' '
	but rev-list --objects --filter=blob:none HEAD >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter=blob:none HEAD >actual &&
	test_bitmap_traversal expect actual
'

test_expect_success 'blob:none filter with specified blob' '
	but rev-list --objects --filter=blob:none HEAD HEAD:two.t >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter=blob:none HEAD HEAD:two.t >actual &&
	test_bitmap_traversal expect actual
'

test_expect_success 'blob:limit filter' '
	but rev-list --objects --filter=blob:limit=5 HEAD >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter=blob:limit=5 HEAD >actual &&
	test_bitmap_traversal expect actual
'

test_expect_success 'blob:limit filter with specified blob' '
	but rev-list --objects --filter=blob:limit=5 \
		     HEAD HEAD:much-larger-blob-two.t >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter=blob:limit=5 \
		     HEAD HEAD:much-larger-blob-two.t >actual &&
	test_bitmap_traversal expect actual
'

test_expect_success 'tree:0 filter' '
	but rev-list --objects --filter=tree:0 HEAD >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter=tree:0 HEAD >actual &&
	test_bitmap_traversal expect actual
'

test_expect_success 'tree:0 filter with specified blob, tree' '
	but rev-list --objects --filter=tree:0 HEAD HEAD:two.t >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter=tree:0 HEAD HEAD:two.t >actual &&
	test_bitmap_traversal expect actual
'

test_expect_success 'tree:1 filter' '
	but rev-list --objects --filter=tree:1 HEAD >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter=tree:1 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'object:type filter' '
	but rev-list --objects --filter=object:type=tag tag >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter=object:type=tag tag >actual &&
	test_cmp expect actual &&

	but rev-list --objects --filter=object:type=cummit tag >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter=object:type=cummit tag >actual &&
	test_bitmap_traversal expect actual &&

	but rev-list --objects --filter=object:type=tree tag >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter=object:type=tree tag >actual &&
	test_bitmap_traversal expect actual &&

	but rev-list --objects --filter=object:type=blob tag >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter=object:type=blob tag >actual &&
	test_bitmap_traversal expect actual
'

test_expect_success 'object:type filter with --filter-provided-objects' '
	but rev-list --objects --filter-provided-objects --filter=object:type=tag tag >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter-provided-objects --filter=object:type=tag tag >actual &&
	test_cmp expect actual &&

	but rev-list --objects --filter-provided-objects --filter=object:type=cummit tag >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter-provided-objects --filter=object:type=cummit tag >actual &&
	test_bitmap_traversal expect actual &&

	but rev-list --objects --filter-provided-objects --filter=object:type=tree tag >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter-provided-objects --filter=object:type=tree tag >actual &&
	test_bitmap_traversal expect actual &&

	but rev-list --objects --filter-provided-objects --filter=object:type=blob tag >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter-provided-objects --filter=object:type=blob tag >actual &&
	test_bitmap_traversal expect actual
'

test_expect_success 'combine filter' '
	but rev-list --objects --filter=blob:limit=1000 --filter=object:type=blob tag >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter=blob:limit=1000 --filter=object:type=blob tag >actual &&
	test_bitmap_traversal expect actual
'

test_expect_success 'combine filter with --filter-provided-objects' '
	but rev-list --objects --filter-provided-objects --filter=blob:limit=1000 --filter=object:type=blob tag >expect &&
	but rev-list --use-bitmap-index \
		     --objects --filter-provided-objects --filter=blob:limit=1000 --filter=object:type=blob tag >actual &&
	test_bitmap_traversal expect actual &&

	but cat-file --batch-check="%(objecttype) %(objectsize)" <actual >objects &&
	while read objecttype objectsize
	do
		test "$objecttype" = blob || return 1
		test "$objectsize" -le 1000 || return 1
	done <objects
'

test_done
