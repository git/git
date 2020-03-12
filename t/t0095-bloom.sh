#!/bin/sh

test_description='test bloom.c'
. ./test-lib.sh

test_expect_success 'compute bloom key for empty string' '
	cat >expect <<-\EOF &&
	Hashes:5615800c|5b966560|61174ab4|66983008|6c19155c|7199fab0|771ae004|
	Filter_Length:2
	Filter_Data:11|11|
	EOF
	test-tool bloom generate_filter "" >actual &&
	test_cmp expect actual
'

test_expect_success 'compute bloom key for whitespace' '
	cat >expect <<-\EOF &&
	Hashes:1bf014e6|8a91b50b|f9335530|67d4f555|d676957a|4518359f|b3b9d5c4|
	Filter_Length:2
	Filter_Data:71|8c|
	EOF
	test-tool bloom generate_filter " " >actual &&
	test_cmp expect actual
'

test_expect_success 'compute bloom key for a root level folder' '
	cat >expect <<-\EOF &&
	Hashes:1a21016f|fff1c06d|e5c27f6b|cb933e69|b163fd67|9734bc65|7d057b63|
	Filter_Length:2
	Filter_Data:a8|aa|
	EOF
	test-tool bloom generate_filter "A" >actual &&
	test_cmp expect actual
'

test_expect_success 'compute bloom key for a root level file' '
	cat >expect <<-\EOF &&
	Hashes:e2d51107|30970605|7e58fb03|cc1af001|19dce4ff|679ed9fd|b560cefb|
	Filter_Length:2
	Filter_Data:aa|a8|
	EOF
	test-tool bloom generate_filter "file.txt" >actual &&
	test_cmp expect actual
'

test_expect_success 'compute bloom key for a deep folder' '
	cat >expect <<-\EOF &&
	Hashes:864cf838|27f055cd|c993b362|6b3710f7|0cda6e8c|ae7dcc21|502129b6|
	Filter_Length:2
	Filter_Data:c6|31|
	EOF
	test-tool bloom generate_filter "A/B/C/D/E" >actual &&
	test_cmp expect actual
'

test_expect_success 'compute bloom key for a deep file' '
	cat >expect <<-\EOF &&
	Hashes:07cdf850|4af629c7|8e1e5b3e|d1468cb5|146ebe2c|5796efa3|9abf211a|
	Filter_Length:2
	Filter_Data:a9|54|
	EOF
	test-tool bloom generate_filter "A/B/C/D/E/file.txt" >actual &&
	test_cmp expect actual
'

test_expect_success 'get bloom filters for commit with no changes' '
	git init &&
	git commit --allow-empty -m "c0" &&
	cat >expect <<-\EOF &&
	Filter_Length:0
	Filter_Data:
	EOF
	test-tool bloom get_filter_for_commit "$(git rev-parse HEAD)" >actual &&
	test_cmp expect actual
'

test_expect_success 'get bloom filter for commit with 10 changes' '
	rm actual &&
	rm expect &&
	mkdir smallDir &&
	for i in $(test_seq 0 9)
	do
		echo $i >smallDir/$i
	done &&
	git add smallDir &&
	git commit -m "commit with 10 changes" &&
	cat >expect <<-\EOF &&
	Filter_Length:25
	Filter_Data:c2|0b|b8|c0|10|88|f0|1d|c1|0c|01|a4|01|28|81|80|01|30|10|d0|92|be|88|10|8a|
	EOF
	test-tool bloom get_filter_for_commit "$(git rev-parse HEAD)" >actual &&
	test_cmp expect actual
'

test_expect_success EXPENSIVE 'get bloom filter for commit with 513 changes' '
	rm actual &&
	rm expect &&
	mkdir bigDir &&
	for i in $(test_seq 0 512)
	do
		echo $i >bigDir/$i
	done &&
	git add bigDir &&
	git commit -m "commit with 513 changes" &&
	cat >expect <<-\EOF &&
	Filter_Length:0
	Filter_Data:
	EOF
	test-tool bloom get_filter_for_commit "$(git rev-parse HEAD)" >actual &&
	test_cmp expect actual
'

test_done
