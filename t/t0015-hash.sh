#!/bin/sh

test_description='test basic hash implementation'

TEST_PASSES_SANITIZE_LEAK=true
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
	perl -e "$| = 1; print q{aaaaaaaaaa} for 1..100000;" |
		test-tool sha1 >actual &&
	grep 34aa973cd4c4daa4f61eeb2bdbad27316534016f actual &&
	printf "blob 0\0" | test-tool sha1 >actual &&
	grep e69de29bb2d1d6434b8b29ae775ad8c2e48c5391 actual &&
	printf "blob 3\0abc" | test-tool sha1 >actual &&
	grep f2ba8f84ab5c1bce84a7b441cb1959cfc7093b7f actual &&
	printf "tree 0\0" | test-tool sha1 >actual &&
	grep 4b825dc642cb6eb9a060e54bf8d69288fbee4904 actual
'

test_expect_success 'test basic SHA-256 hash values' '
	test-tool sha256 </dev/null >actual &&
	grep e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 actual &&
	printf "a" | test-tool sha256 >actual &&
	grep ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb actual &&
	printf "abc" | test-tool sha256 >actual &&
	grep ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad actual &&
	printf "message digest" | test-tool sha256 >actual &&
	grep f7846f55cf23e14eebeab5b4e1550cad5b509e3348fbc4efa3a1413d393cb650 actual &&
	printf "abcdefghijklmnopqrstuvwxyz" | test-tool sha256 >actual &&
	grep 71c480df93d6ae2f1efad1447c66c9525e316218cf51fc8d9ed832f2daf18b73 actual &&
	# Try to exercise the chunking code by turning autoflush on.
	perl -e "$| = 1; print q{aaaaaaaaaa} for 1..100000;" |
		test-tool sha256 >actual &&
	grep cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0 actual &&
	perl -e "$| = 1; print q{abcdefghijklmnopqrstuvwxyz} for 1..100000;" |
		test-tool sha256 >actual &&
	grep e406ba321ca712ad35a698bf0af8d61fc4dc40eca6bdcea4697962724ccbde35 actual &&
	printf "blob 0\0" | test-tool sha256 >actual &&
	grep 473a0f4c3be8a93681a267e3b1e9a7dcda1185436fe141f7749120a303721813 actual &&
	printf "blob 3\0abc" | test-tool sha256 >actual &&
	grep c1cf6e465077930e88dc5136641d402f72a229ddd996f627d60e9639eaba35a6 actual &&
	printf "tree 0\0" | test-tool sha256 >actual &&
	grep 6ef19b41225c5369f1c104d45d8d85efa9b057b53b14b4b9b939dd74decc5321 actual
'

test_done
