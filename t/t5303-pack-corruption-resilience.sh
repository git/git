#!/bin/sh
#
# Copyright (c) 2008 Nicolas Pitre
#

test_description='resilience to pack corruptions with redundant objects'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# Note: the test objects are created with knowledge of their pack encoding
# to ensure good code path coverage, and to facilitate direct alteration
# later on.  The assumed characteristics are:
#
# 1) blob_2 is a delta with blob_1 for base and blob_3 is a delta with blob2
#    for base, such that blob_3 delta depth is 2;
#
# 2) the bulk of object data is uncompressible so the text part remains
#    visible;
#
# 3) object header is always 2 bytes.

create_test_files() {
    test-tool genrandom "foo" 2000 > file_1 &&
    test-tool genrandom "foo" 1800 > file_2 &&
    test-tool genrandom "foo" 1800 > file_3 &&
    echo " base " >> file_1 &&
    echo " delta1 " >> file_2 &&
    echo " delta delta2 " >> file_3 &&
    test-tool genrandom "bar" 150 >> file_2 &&
    test-tool genrandom "baz" 100 >> file_3
}

create_new_pack() {
    rm -rf .git &&
    git init &&
    blob_1=$(git hash-object -t blob -w file_1) &&
    blob_2=$(git hash-object -t blob -w file_2) &&
    blob_3=$(git hash-object -t blob -w file_3) &&
    pack=$(printf "$blob_1\n$blob_2\n$blob_3\n" |
          git pack-objects $@ .git/objects/pack/pack) &&
    pack=".git/objects/pack/pack-${pack}" &&
    git verify-pack -v ${pack}.pack
}

do_repack() {
    for f in $pack.*
    do
	    mv $f "$(echo $f | sed -e 's/pack-/pack-corrupt-/')" || return 1
    done &&
    pack=$(printf "$blob_1\n$blob_2\n$blob_3\n" |
          git pack-objects $@ .git/objects/pack/pack) &&
    pack=".git/objects/pack/pack-${pack}" &&
    rm -f .git/objects/pack/pack-corrupt-*
}

do_corrupt_object() {
    ofs=$(git show-index < ${pack}.idx | grep $1 | cut -f1 -d" ") &&
    ofs=$(($ofs + $2)) &&
    chmod +w ${pack}.pack &&
    dd of=${pack}.pack bs=1 conv=notrunc seek=$ofs &&
    test_must_fail git verify-pack ${pack}.pack
}

printf '\0' > zero

test_expect_success 'initial setup validation' '
	create_test_files &&
	create_new_pack &&
	git prune-packed &&
	git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success 'create corruption in header of first object' '
	do_corrupt_object $blob_1 0 < zero &&
	test_must_fail git cat-file blob $blob_1 > /dev/null &&
	test_must_fail git cat-file blob $blob_2 > /dev/null &&
	test_must_fail git cat-file blob $blob_3 > /dev/null
'

test_expect_success '... but having a loose copy allows for full recovery' '
	mv ${pack}.idx tmp &&
	git hash-object -t blob -w file_1 &&
	mv tmp ${pack}.idx &&
	git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success '... and loose copy of first delta allows for partial recovery' '
	git prune-packed &&
	test_must_fail git cat-file blob $blob_2 > /dev/null &&
	mv ${pack}.idx tmp &&
	git hash-object -t blob -w file_2 &&
	mv tmp ${pack}.idx &&
	test_must_fail git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success 'create corruption in data of first object' '
	create_new_pack &&
	git prune-packed &&
	chmod +w ${pack}.pack &&
	perl -i.bak -pe "s/ base /abcdef/" ${pack}.pack &&
	test_must_fail git cat-file blob $blob_1 > /dev/null &&
	test_must_fail git cat-file blob $blob_2 > /dev/null &&
	test_must_fail git cat-file blob $blob_3 > /dev/null
'

test_expect_success '... but having a loose copy allows for full recovery' '
	mv ${pack}.idx tmp &&
	git hash-object -t blob -w file_1 &&
	mv tmp ${pack}.idx &&
	git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success '... and loose copy of second object allows for partial recovery' '
	git prune-packed &&
	test_must_fail git cat-file blob $blob_2 > /dev/null &&
	mv ${pack}.idx tmp &&
	git hash-object -t blob -w file_2 &&
	mv tmp ${pack}.idx &&
	test_must_fail git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success 'create corruption in header of first delta' '
	create_new_pack &&
	git prune-packed &&
	do_corrupt_object $blob_2 0 < zero &&
	git cat-file blob $blob_1 > /dev/null &&
	test_must_fail git cat-file blob $blob_2 > /dev/null &&
	test_must_fail git cat-file blob $blob_3 > /dev/null
'

test_expect_success '... but having a loose copy allows for full recovery' '
	mv ${pack}.idx tmp &&
	git hash-object -t blob -w file_2 &&
	mv tmp ${pack}.idx &&
	git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success '... and then a repack "clears" the corruption' '
	do_repack &&
	git prune-packed &&
	git verify-pack ${pack}.pack &&
	git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success 'create corruption in data of first delta' '
	create_new_pack &&
	git prune-packed &&
	chmod +w ${pack}.pack &&
	perl -i.bak -pe "s/ delta1 /abcdefgh/" ${pack}.pack &&
	git cat-file blob $blob_1 > /dev/null &&
	test_must_fail git cat-file blob $blob_2 > /dev/null &&
	test_must_fail git cat-file blob $blob_3 > /dev/null
'

test_expect_success '... but having a loose copy allows for full recovery' '
	mv ${pack}.idx tmp &&
	git hash-object -t blob -w file_2 &&
	mv tmp ${pack}.idx &&
	git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success '... and then a repack "clears" the corruption' '
	do_repack &&
	git prune-packed &&
	git verify-pack ${pack}.pack &&
	git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success 'corruption in delta base reference of first delta (OBJ_REF_DELTA)' '
	create_new_pack &&
	git prune-packed &&
	do_corrupt_object $blob_2 2 < zero &&
	git cat-file blob $blob_1 > /dev/null &&
	test_must_fail git cat-file blob $blob_2 > /dev/null &&
	test_must_fail git cat-file blob $blob_3 > /dev/null
'

test_expect_success '... but having a loose copy allows for full recovery' '
	mv ${pack}.idx tmp &&
	git hash-object -t blob -w file_2 &&
	mv tmp ${pack}.idx &&
	git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success '... and then a repack "clears" the corruption' '
	do_repack &&
	git prune-packed &&
	git verify-pack ${pack}.pack &&
	git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success 'corruption #0 in delta base reference of first delta (OBJ_OFS_DELTA)' '
	create_new_pack --delta-base-offset &&
	git prune-packed &&
	do_corrupt_object $blob_2 2 < zero &&
	git cat-file blob $blob_1 > /dev/null &&
	test_must_fail git cat-file blob $blob_2 > /dev/null &&
	test_must_fail git cat-file blob $blob_3 > /dev/null
'

test_expect_success '... but having a loose copy allows for full recovery' '
	mv ${pack}.idx tmp &&
	git hash-object -t blob -w file_2 &&
	mv tmp ${pack}.idx &&
	git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success '... and then a repack "clears" the corruption' '
	do_repack --delta-base-offset &&
	git prune-packed &&
	git verify-pack ${pack}.pack &&
	git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success 'corruption #1 in delta base reference of first delta (OBJ_OFS_DELTA)' '
	create_new_pack --delta-base-offset &&
	git prune-packed &&
	printf "\001" | do_corrupt_object $blob_2 2 &&
	git cat-file blob $blob_1 > /dev/null &&
	test_must_fail git cat-file blob $blob_2 > /dev/null &&
	test_must_fail git cat-file blob $blob_3 > /dev/null
'

test_expect_success '... but having a loose copy allows for full recovery' '
	mv ${pack}.idx tmp &&
	git hash-object -t blob -w file_2 &&
	mv tmp ${pack}.idx &&
	git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success '... and then a repack "clears" the corruption' '
	do_repack --delta-base-offset &&
	git prune-packed &&
	git verify-pack ${pack}.pack &&
	git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success '... and a redundant pack allows for full recovery too' '
	do_corrupt_object $blob_2 2 < zero &&
	git cat-file blob $blob_1 > /dev/null &&
	test_must_fail git cat-file blob $blob_2 > /dev/null &&
	test_must_fail git cat-file blob $blob_3 > /dev/null &&
	mv ${pack}.idx tmp &&
	git hash-object -t blob -w file_1 &&
	git hash-object -t blob -w file_2 &&
	printf "$blob_1\n$blob_2\n" | git pack-objects .git/objects/pack/pack &&
	git prune-packed &&
	mv tmp ${pack}.idx &&
	git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success 'corruption of delta base reference pointing to wrong object' '
	create_new_pack --delta-base-offset &&
	git prune-packed &&
	printf "\220\033" | do_corrupt_object $blob_3 2 &&
	git cat-file blob $blob_1 >/dev/null &&
	git cat-file blob $blob_2 >/dev/null &&
	test_must_fail git cat-file blob $blob_3 >/dev/null
'

test_expect_success '... but having a loose copy allows for full recovery' '
	mv ${pack}.idx tmp &&
	git hash-object -t blob -w file_3 &&
	mv tmp ${pack}.idx &&
	git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success '... and then a repack "clears" the corruption' '
	do_repack --delta-base-offset --no-reuse-delta &&
	git prune-packed &&
	git verify-pack ${pack}.pack &&
	git cat-file blob $blob_1 > /dev/null &&
	git cat-file blob $blob_2 > /dev/null &&
	git cat-file blob $blob_3 > /dev/null
'

test_expect_success 'corrupting header to have too small output buffer fails unpack' '
	create_new_pack &&
	git prune-packed &&
	printf "\262\001" | do_corrupt_object $blob_1 0 &&
	test_must_fail git cat-file blob $blob_1 > /dev/null &&
	test_must_fail git cat-file blob $blob_2 > /dev/null &&
	test_must_fail git cat-file blob $blob_3 > /dev/null
'

# \0 - empty base
# \1 - one byte in result
# \1 - one literal byte (X)
test_expect_success 'apply good minimal delta' '
	printf "\0\1\1X" > minimal_delta &&
	test-tool delta -p /dev/null minimal_delta /dev/null
'

# \0 - empty base
# \1 - 1 byte in result
# \2 - two literal bytes (one too many)
test_expect_success 'apply delta with too many literal bytes' '
	printf "\0\1\2XX" > too_big_literal &&
	test_must_fail test-tool delta -p /dev/null too_big_literal /dev/null
'

# \4 - four bytes in base
# \1 - one byte in result
# \221 - copy, one byte offset, one byte size
#   \0 - copy from offset 0
#   \2 - copy two bytes (one too many)
test_expect_success 'apply delta with too many copied bytes' '
	printf "\4\1\221\0\2" > too_big_copy &&
	printf base >base &&
	test_must_fail test-tool delta -p base too_big_copy /dev/null
'

# \0 - empty base
# \2 - two bytes in result
# \2 - two literal bytes (we are short one)
test_expect_success 'apply delta with too few literal bytes' '
	printf "\0\2\2X" > truncated_delta &&
	test_must_fail test-tool delta -p /dev/null truncated_delta /dev/null
'

# \0 - empty base
# \1 - one byte in result
# \221 - copy, one byte offset, one byte size
#   \0 - copy from offset 0
#   \1 - copy one byte (we are short one)
test_expect_success 'apply delta with too few bytes in base' '
	printf "\0\1\221\0\1" > truncated_base &&
	test_must_fail test-tool delta -p /dev/null truncated_base /dev/null
'

# \4 - four bytes in base
# \2 - two bytes in result
# \1 - one literal byte (X)
# \221 - copy, one byte offset, one byte size
#        (offset/size missing)
#
# Note that the literal byte is necessary to get past the uninteresting minimum
# delta size check.
test_expect_success 'apply delta with truncated copy parameters' '
	printf "\4\2\1X\221" > truncated_copy_delta &&
	printf base >base &&
	test_must_fail test-tool delta -p base truncated_copy_delta /dev/null
'

# \0 - empty base
# \1 - one byte in result
# \1 - one literal byte (X)
# \1 - trailing garbage command
test_expect_success 'apply delta with trailing garbage literal' '
	printf "\0\1\1X\1" > tail_garbage_literal &&
	test_must_fail test-tool delta -p /dev/null tail_garbage_literal /dev/null
'

# \4 - four bytes in base
# \1 - one byte in result
# \1 - one literal byte (X)
# \221 - copy, one byte offset, one byte size
#   \0 - copy from offset 0
#   \1 - copy 1 byte
test_expect_success 'apply delta with trailing garbage copy' '
	printf "\4\1\1X\221\0\1" > tail_garbage_copy &&
	printf base >base &&
	test_must_fail test-tool delta -p /dev/null tail_garbage_copy /dev/null
'

# \0 - empty base
# \1 - one byte in result
# \1 - one literal byte (X)
# \0 - bogus opcode
test_expect_success 'apply delta with trailing garbage opcode' '
	printf "\0\1\1X\0" > tail_garbage_opcode &&
	test_must_fail test-tool delta -p /dev/null tail_garbage_opcode /dev/null
'

test_done
