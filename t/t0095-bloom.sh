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

test_done