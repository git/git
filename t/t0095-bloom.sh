#!/bin/sh

test_description='Testing the various Bloom filter computations in bloom.c'
. ./test-lib.sh

test_expect_success 'compute unseeded murmur3 hash for empty string' '
	cat >expect <<-\EOF &&
	Murmur3 Hash with seed=0:0x00000000
	EOF
	test-tool bloom get_murmur3 "" >actual &&
	test_cmp expect actual
'

test_expect_success 'compute unseeded murmur3 hash for test string 1' '
	cat >expect <<-\EOF &&
	Murmur3 Hash with seed=0:0x627b0c2c
	EOF
	test-tool bloom get_murmur3 "Hello world!" >actual &&
	test_cmp expect actual
'

test_expect_success 'compute unseeded murmur3 hash for test string 2' '
	cat >expect <<-\EOF &&
	Murmur3 Hash with seed=0:0x2e4ff723
	EOF
	test-tool bloom get_murmur3 "The quick brown fox jumps over the lazy dog" >actual &&
	test_cmp expect actual
'

test_expect_success 'compute bloom key for empty string' '
	cat >expect <<-\EOF &&
	Hashes:0x5615800c|0x5b966560|0x61174ab4|0x66983008|0x6c19155c|0x7199fab0|0x771ae004|
	Filter_Length:2
	Filter_Data:11|11|
	EOF
	test-tool bloom generate_filter "" >actual &&
	test_cmp expect actual
'

test_expect_success 'compute bloom key for whitespace' '
	cat >expect <<-\EOF &&
	Hashes:0xf178874c|0x5f3d6eb6|0xcd025620|0x3ac73d8a|0xa88c24f4|0x16510c5e|0x8415f3c8|
	Filter_Length:2
	Filter_Data:51|55|
	EOF
	test-tool bloom generate_filter " " >actual &&
	test_cmp expect actual
'

test_expect_success 'compute bloom key for test string 1' '
	cat >expect <<-\EOF &&
	Hashes:0xb270de9b|0x1bb6f26e|0x84fd0641|0xee431a14|0x57892de7|0xc0cf41ba|0x2a15558d|
	Filter_Length:2
	Filter_Data:92|6c|
	EOF
	test-tool bloom generate_filter "Hello world!" >actual &&
	test_cmp expect actual
'

test_expect_success 'compute bloom key for test string 2' '
	cat >expect <<-\EOF &&
	Hashes:0x20ab385b|0xf5237fe2|0xc99bc769|0x9e140ef0|0x728c5677|0x47049dfe|0x1b7ce585|
	Filter_Length:2
	Filter_Data:a5|4a|
	EOF
	test-tool bloom generate_filter "file.txt" >actual &&
	test_cmp expect actual
'

test_expect_success 'get bloom filters for commit with no changes' '
	git init &&
	git commit --allow-empty -m "c0" &&
	cat >expect <<-\EOF &&
	Filter_Length:1
	Filter_Data:00|
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
	Filter_Length:14
	Filter_Data:02|b3|c4|a0|34|e7|fe|eb|cb|47|fe|a0|e8|72|
	EOF
	test-tool bloom get_filter_for_commit "$(git rev-parse HEAD)" >actual &&
	test_cmp expect actual
'

test_expect_success EXPENSIVE 'get bloom filter for commit with 513 changes' '
	rm actual &&
	rm expect &&
	mkdir bigDir &&
	for i in $(test_seq 0 511)
	do
		echo $i >bigDir/$i
	done &&
	git add bigDir &&
	git commit -m "commit with 513 changes" &&
	cat >expect <<-\EOF &&
	Filter_Length:1
	Filter_Data:ff|
	EOF
	test-tool bloom get_filter_for_commit "$(git rev-parse HEAD)" >actual &&
	test_cmp expect actual
'

test_done
