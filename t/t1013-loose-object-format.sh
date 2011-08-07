#!/bin/sh
#
# Copyright (c) 2011 Roberto Tyley
#

test_description='Correctly identify and parse loose object headers

There are two file formats for loose objects - the original standard
format, and the experimental format introduced with Git v1.4.3, later
deprecated with v1.5.3. Although Git no longer writes the
experimental format, objects in both formats must be read, with the
format for a given file being determined by the header.

Detecting file format based on header is not entirely trivial, not
least because the first byte of a zlib-deflated stream will vary
depending on how much memory was allocated for the deflation window
buffer when the object was written out (for example 4KB on Android,
rather that 32KB on a normal PC).

The loose objects used as test vectors have been generated with the
following Git versions:

standard format: Git v1.7.4.1
experimental format: Git v1.4.3 (legacyheaders=false)
standard format, deflated with 4KB window size: Agit/JGit on Android
'

. ./test-lib.sh
LF='
'

assert_blob_equals() {
	printf "%s" "$2" >expected &&
	git cat-file -p "$1" >actual &&
	test_cmp expected actual
}

test_expect_success setup '
	cp -R "$TEST_DIRECTORY/t1013/objects" .git/
	git --version
'

test_expect_success 'read standard-format loose objects' '
	git cat-file tag 8d4e360d6c70fbd72411991c02a09c442cf7a9fa &&
	git cat-file commit 6baee0540ea990d9761a3eb9ab183003a71c3696 &&
	git ls-tree 7a37b887a73791d12d26c0d3e39568a8fb0fa6e8 &&
	assert_blob_equals "257cc5642cb1a054f08cc83f2d943e56fd3ebe99" "foo$LF"
'

test_expect_success 'read experimental-format loose objects' '
	git cat-file tag 76e7fa9941f4d5f97f64fea65a2cba436bc79cbb &&
	git cat-file commit 7875c6237d3fcdd0ac2f0decc7d3fa6a50b66c09 &&
	git ls-tree 95b1625de3ba8b2214d1e0d0591138aea733f64f &&
	assert_blob_equals "2e65efe2a145dda7ee51d1741299f848e5bf752e" "a" &&
	assert_blob_equals "9ae9e86b7bd6cb1472d9373702d8249973da0832" "ab" &&
	assert_blob_equals "85df50785d62d3b05ab03d9cbf7e4a0b49449730" "abcd" &&
	assert_blob_equals "1656f9233d999f61ef23ef390b9c71d75399f435" "abcdefgh" &&
	assert_blob_equals "1e72a6b2c4a577ab0338860fa9fe87f761fc9bbd" "abcdefghi" &&
	assert_blob_equals "70e6a83d8dcb26fc8bc0cf702e2ddeb6adca18fd" "abcdefghijklmnop" &&
	assert_blob_equals "bd15045f6ce8ff75747562173640456a394412c8" "abcdefghijklmnopqrstuvwx"
'

test_expect_success 'read standard-format objects deflated with smaller window buffer' '
	git cat-file tag f816d5255855ac160652ee5253b06cd8ee14165a &&
	git cat-file tag 149cedb5c46929d18e0f118e9fa31927487af3b6
'

test_done
