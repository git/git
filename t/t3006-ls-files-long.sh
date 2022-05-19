#!/bin/sh

test_description='overly long paths'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	p=filefilefilefilefilefilefilefile &&
	p=$p$p$p$p$p$p$p$p$p$p$p$p$p$p$p$p &&
	p=$p$p$p$p$p$p$p$p$p$p$p$p$p$p$p$p &&

	path_a=${p}_a &&
	path_z=${p}_z &&

	blob_a=$(echo frotz | but hash-object -w --stdin) &&
	blob_z=$(echo nitfol | but hash-object -w --stdin) &&

	pat="100644 %s 0\t%s\n"
'

test_expect_success 'overly-long path by itself is not a problem' '
	printf "$pat" "$blob_a" "$path_a" |
	but update-index --add --index-info &&
	echo "$path_a" >expect &&
	but ls-files >actual &&
	test_cmp expect actual
'

test_expect_success 'overly-long path does not replace another by mistake' '
	printf "$pat" "$blob_a" "$path_a" "$blob_z" "$path_z" |
	but update-index --add --index-info &&
	(
		echo "$path_a" &&
		echo "$path_z"
	) >expect &&
	but ls-files >actual &&
	test_cmp expect actual
'

test_done
