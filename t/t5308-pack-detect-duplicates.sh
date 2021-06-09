#!/bin/sh

test_description='handling of duplicate objects in incoming packfiles'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-pack.sh

test_expect_success 'setup' '
	test_oid_cache <<-EOF
	lo_oid sha1:e68fe8129b546b101aee9510c5328e7f21ca1d18
	lo_oid sha256:471819e8c52bf11513f100b2810a8aa0622d5cd3d1c913758a071dd4b3bad8fe

	missing_oid sha1:e69d000000000000000000000000000000000000
	missing_oid sha256:4720000000000000000000000000000000000000000000000000000000000000
	EOF
'

# The sha1s we have in our pack. It's important that these have the same
# starting byte, so that they end up in the same fanout section of the index.
# That lets us make sure we are exercising the binary search with both sets.
LO_SHA1=$(test_oid lo_oid)
HI_SHA1=$EMPTY_BLOB

# And here's a "missing sha1" which will produce failed lookups. It must also
# be in the same fanout section, and should be between the two (so that during
# our binary search, we are sure to end up looking at one or the other of the
# duplicate runs).
MISSING_SHA1=$(test_oid missing_oid)

# git will never intentionally create packfiles with
# duplicate objects, so we have to construct them by hand.
#
# $1 is the name of the packfile to create
#
# $2 is the number of times to duplicate each object
create_pack () {
	pack_header "$((2 * $2))" >"$1" &&
	for i in $(test_seq 1 "$2"); do
		pack_obj $LO_SHA1 &&
		pack_obj $HI_SHA1
	done >>"$1" &&
	pack_trailer "$1"
}

# double-check that create_pack actually works
test_expect_success 'pack with no duplicates' '
	create_pack no-dups.pack 1 &&
	git index-pack --stdin <no-dups.pack
'

test_expect_success 'index-pack will allow duplicate objects by default' '
	clear_packs &&
	create_pack dups.pack 100 &&
	git index-pack --stdin <dups.pack
'

test_expect_success 'create batch-check test vectors' '
	cat >input <<-EOF &&
	$LO_SHA1
	$HI_SHA1
	$MISSING_SHA1
	EOF
	cat >expect <<-EOF
	$LO_SHA1 blob 2
	$HI_SHA1 blob 0
	$MISSING_SHA1 missing
	EOF
'

test_expect_success 'lookup in duplicated pack' '
	git cat-file --batch-check <input >actual &&
	test_cmp expect actual
'

test_expect_success 'index-pack can reject packs with duplicates' '
	clear_packs &&
	create_pack dups.pack 2 &&
	test_must_fail git index-pack --strict --stdin <dups.pack &&
	test_expect_code 1 git cat-file -e $LO_SHA1
'

test_done
