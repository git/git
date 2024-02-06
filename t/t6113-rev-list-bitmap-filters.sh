#!/bin/sh

test_description='rev-list combining bitmaps and filters'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-bitmap.sh


test_expect_success 'set up bitmapped repo' '
	# one commit will have bitmaps, the other will not
	test_commit one &&
	test_commit much-larger-blob-one &&
	git repack -adb &&
	test_commit two &&
	test_commit much-larger-blob-two &&
	git tag tag
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

test_expect_success 'tree:0 filter' '
	git rev-list --objects --filter=tree:0 HEAD >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter=tree:0 HEAD >actual &&
	test_bitmap_traversal expect actual
'

test_expect_success 'tree:0 filter with specified blob, tree' '
	git rev-list --objects --filter=tree:0 HEAD HEAD:two.t >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter=tree:0 HEAD HEAD:two.t >actual &&
	test_bitmap_traversal expect actual
'

test_expect_success 'tree:1 filter' '
	git rev-list --objects --filter=tree:1 HEAD >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter=tree:1 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'object:type filter' '
	git rev-list --objects --filter=object:type=tag tag >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter=object:type=tag tag >actual &&
	test_cmp expect actual &&

	git rev-list --objects --filter=object:type=commit tag >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter=object:type=commit tag >actual &&
	test_bitmap_traversal expect actual &&

	git rev-list --objects --filter=object:type=tree tag >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter=object:type=tree tag >actual &&
	test_bitmap_traversal expect actual &&

	git rev-list --objects --filter=object:type=blob tag >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter=object:type=blob tag >actual &&
	test_bitmap_traversal expect actual
'

test_expect_success 'object:type filter with --filter-provided-objects' '
	git rev-list --objects --filter-provided-objects --filter=object:type=tag tag >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter-provided-objects --filter=object:type=tag tag >actual &&
	test_cmp expect actual &&

	git rev-list --objects --filter-provided-objects --filter=object:type=commit tag >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter-provided-objects --filter=object:type=commit tag >actual &&
	test_bitmap_traversal expect actual &&

	git rev-list --objects --filter-provided-objects --filter=object:type=tree tag >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter-provided-objects --filter=object:type=tree tag >actual &&
	test_bitmap_traversal expect actual &&

	git rev-list --objects --filter-provided-objects --filter=object:type=blob tag >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter-provided-objects --filter=object:type=blob tag >actual &&
	test_bitmap_traversal expect actual
'

test_expect_success 'combine filter' '
	git rev-list --objects --filter=blob:limit=1000 --filter=object:type=blob tag >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter=blob:limit=1000 --filter=object:type=blob tag >actual &&
	test_bitmap_traversal expect actual
'

test_expect_success 'combine filter with --filter-provided-objects' '
	git rev-list --objects --filter-provided-objects --filter=blob:limit=1000 --filter=object:type=blob tag >expect &&
	git rev-list --use-bitmap-index \
		     --objects --filter-provided-objects --filter=blob:limit=1000 --filter=object:type=blob tag >actual &&
	test_bitmap_traversal expect actual &&

	git cat-file --batch-check="%(objecttype) %(objectsize)" <actual >objects &&
	while read objecttype objectsize
	do
		test "$objecttype" = blob || return 1
		test "$objectsize" -le 1000 || return 1
	done <objects
'

test_expect_success 'bitmap traversal with --unpacked' '
	git repack -adb &&
	test_commit unpacked &&

	git rev-list --objects --no-object-names unpacked^.. >expect.raw &&
	sort expect.raw >expect &&

	git rev-list --use-bitmap-index --objects --all --unpacked >actual.raw &&
	sort actual.raw >actual &&

	test_cmp expect actual
'

test_done
