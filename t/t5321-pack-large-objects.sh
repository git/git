#!/bin/sh
#
# Copyright (c) 2018 Johannes Schindelin
#

test_description='git pack-object with "large" deltas

'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-pack.sh

# Two similar-ish objects that we have computed deltas between.
A=$(test_oid packlib_7_0)
B=$(test_oid packlib_7_76)

test_expect_success 'setup' '
	clear_packs &&
	{
		pack_header 2 &&
		pack_obj $A $B &&
		pack_obj $B
	} >ab.pack &&
	pack_trailer ab.pack &&
	git index-pack --stdin <ab.pack
'

test_expect_success 'repack large deltas' '
	printf "%s\\n" $A $B |
	GIT_TEST_OE_DELTA_SIZE=2 git pack-objects tmp-pack
'

test_done
