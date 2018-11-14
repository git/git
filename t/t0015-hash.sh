#!/bin/sh

test_description='test basic hash implementation'
. ./test-lib.sh


test_expect_success 'test basic SHA-1 hash values' '
	test-tool sha1 </dev/null >actual &&
	grep da39a3ee5e6b4b0d3255bfef95601890afd80709 actual &&
	printf "a" | test-tool sha1 >actual &&
	grep 86f7e437faa5a7fce15d1ddcb9eaeaea377667b8 actual &&
	printf "abc" | test-tool sha1 >actual &&
	grep a9993e364706816aba3e25717850c26c9cd0d89d actual &&
	printf "message digest" | test-tool sha1 >actual &&
	grep c12252ceda8be8994d5fa0290a47231c1d16aae3 actual &&
	printf "abcdefghijklmnopqrstuvwxyz" | test-tool sha1 >actual &&
	grep 32d10c7b8cf96570ca04ce37f2a19d84240d3a89 actual &&
	perl -e "$| = 1; print q{aaaaaaaaaa} for 1..100000;" | \
		test-tool sha1 >actual &&
	grep 34aa973cd4c4daa4f61eeb2bdbad27316534016f actual &&
	printf "blob 0\0" | test-tool sha1 >actual &&
	grep e69de29bb2d1d6434b8b29ae775ad8c2e48c5391 actual &&
	printf "blob 3\0abc" | test-tool sha1 >actual &&
	grep f2ba8f84ab5c1bce84a7b441cb1959cfc7093b7f actual &&
	printf "tree 0\0" | test-tool sha1 >actual &&
	grep 4b825dc642cb6eb9a060e54bf8d69288fbee4904 actual
'

test_done
