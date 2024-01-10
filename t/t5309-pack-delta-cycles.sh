#!/bin/sh

test_description='test index-pack handling of delta cycles in packfiles'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-pack.sh

# Two similar-ish objects that we have computed deltas between.
A=$(test_oid packlib_7_0)
B=$(test_oid packlib_7_76)

# double-check our hand-constucted packs
test_expect_success 'index-pack works with a single delta (A->B)' '
	clear_packs &&
	{
		pack_header 2 &&
		pack_obj $A $B &&
		pack_obj $B
	} >ab.pack &&
	pack_trailer ab.pack &&
	git index-pack --stdin <ab.pack &&
	git cat-file -t $A &&
	git cat-file -t $B
'

test_expect_success 'index-pack works with a single delta (B->A)' '
	clear_packs &&
	{
		pack_header 2 &&
		pack_obj $A &&
		pack_obj $B $A
	} >ba.pack &&
	pack_trailer ba.pack &&
	git index-pack --stdin <ba.pack &&
	git cat-file -t $A &&
	git cat-file -t $B
'

test_expect_success 'index-pack detects missing base objects' '
	clear_packs &&
	{
		pack_header 1 &&
		pack_obj $A $B
	} >missing.pack &&
	pack_trailer missing.pack &&
	test_must_fail git index-pack --fix-thin --stdin <missing.pack
'

test_expect_success 'index-pack detects REF_DELTA cycles' '
	clear_packs &&
	{
		pack_header 2 &&
		pack_obj $A $B &&
		pack_obj $B $A
	} >cycle.pack &&
	pack_trailer cycle.pack &&
	test_must_fail git index-pack --fix-thin --stdin <cycle.pack
'

test_expect_success 'failover to an object in another pack' '
	clear_packs &&
	git index-pack --stdin <ab.pack &&
	test_must_fail git index-pack --stdin --fix-thin <cycle.pack
'

test_expect_success 'failover to a duplicate object in the same pack' '
	clear_packs &&
	{
		pack_header 3 &&
		pack_obj $A $B &&
		pack_obj $B $A &&
		pack_obj $A
	} >recoverable.pack &&
	pack_trailer recoverable.pack &&
	test_must_fail git index-pack --fix-thin --stdin <recoverable.pack
'

test_done
