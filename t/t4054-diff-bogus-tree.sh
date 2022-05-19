#!/bin/sh

test_description='test diff with a bogus tree containing the null sha1'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'create bogus tree' '
	name=$(echo $ZERO_OID | sed -e "s/00/Q/g") &&
	bogus_tree=$(
		printf "100644 fooQ$name" |
		q_to_nul |
		but hash-object -w --stdin -t tree
	)
'

test_expect_success 'create tree with matching file' '
	echo bar >foo &&
	but add foo &&
	good_tree=$(but write-tree) &&
	blob=$(but rev-parse :foo)
'

test_expect_success 'raw diff shows null sha1 (addition)' '
	echo ":000000 100644 $ZERO_OID $ZERO_OID A	foo" >expect &&
	but diff-tree $EMPTY_TREE $bogus_tree >actual &&
	test_cmp expect actual
'

test_expect_success 'raw diff shows null sha1 (removal)' '
	echo ":100644 000000 $ZERO_OID $ZERO_OID D	foo" >expect &&
	but diff-tree $bogus_tree $EMPTY_TREE >actual &&
	test_cmp expect actual
'

test_expect_success 'raw diff shows null sha1 (modification)' '
	echo ":100644 100644 $blob $ZERO_OID M	foo" >expect &&
	but diff-tree $good_tree $bogus_tree >actual &&
	test_cmp expect actual
'

test_expect_success 'raw diff shows null sha1 (other direction)' '
	echo ":100644 100644 $ZERO_OID $blob M	foo" >expect &&
	but diff-tree $bogus_tree $good_tree >actual &&
	test_cmp expect actual
'

test_expect_success 'raw diff shows null sha1 (reverse)' '
	echo ":100644 100644 $ZERO_OID $blob M	foo" >expect &&
	but diff-tree -R $good_tree $bogus_tree >actual &&
	test_cmp expect actual
'

test_expect_success 'raw diff shows null sha1 (index)' '
	echo ":100644 100644 $ZERO_OID $blob M	foo" >expect &&
	but diff-index $bogus_tree >actual &&
	test_cmp expect actual
'

test_expect_success 'patch fails due to bogus sha1 (addition)' '
	test_must_fail but diff-tree -p $EMPTY_TREE $bogus_tree
'

test_expect_success 'patch fails due to bogus sha1 (removal)' '
	test_must_fail but diff-tree -p $bogus_tree $EMPTY_TREE
'

test_expect_success 'patch fails due to bogus sha1 (modification)' '
	test_must_fail but diff-tree -p $good_tree $bogus_tree
'

test_expect_success 'patch fails due to bogus sha1 (other direction)' '
	test_must_fail but diff-tree -p $bogus_tree $good_tree
'

test_expect_success 'patch fails due to bogus sha1 (reverse)' '
	test_must_fail but diff-tree -R -p $good_tree $bogus_tree
'

test_expect_success 'patch fails due to bogus sha1 (index)' '
	test_must_fail but diff-index -p $bogus_tree
'

test_done
