#!/bin/sh
#
# Copyright (c) 2008 Nicolas Pitre
#

test_description='resilience to pack corruptions with redundant objects'
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
    test-genrandom "foo" 2000 > file_1 &&
    test-genrandom "foo" 1800 > file_2 &&
    test-genrandom "foo" 1800 > file_3 &&
    echo " base " >> file_1 &&
    echo " delta1 " >> file_2 &&
    echo " delta delta2 " >> file_3 &&
    test-genrandom "bar" 150 >> file_2 &&
    test-genrandom "baz" 100 >> file_3
}

create_new_pack() {
    rm -rf .git &&
    git init &&
    blob_1=`git hash-object -t blob -w file_1` &&
    blob_2=`git hash-object -t blob -w file_2` &&
    blob_3=`git hash-object -t blob -w file_3` &&
    pack=`printf "$blob_1\n$blob_2\n$blob_3\n" |
          git pack-objects $@ .git/objects/pack/pack` &&
    pack=".git/objects/pack/pack-${pack}" &&
    git verify-pack -v ${pack}.pack
}

do_repack() {
    pack=`printf "$blob_1\n$blob_2\n$blob_3\n" |
          git pack-objects $@ .git/objects/pack/pack` &&
    pack=".git/objects/pack/pack-${pack}"
}

do_corrupt_object() {
    ofs=`git show-index < ${pack}.idx | grep $1 | cut -f1 -d" "` &&
    ofs=$(($ofs + $2)) &&
    chmod +w ${pack}.pack &&
    dd of=${pack}.pack bs=1 conv=notrunc seek=$ofs &&
    test_must_fail git verify-pack ${pack}.pack
}

printf '\0' > zero

test_expect_success \
    'initial setup validation' \
    'create_test_files &&
     create_new_pack &&
     git prune-packed &&
     git cat-file blob $blob_1 > /dev/null &&
     git cat-file blob $blob_2 > /dev/null &&
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    'create corruption in header of first object' \
    'do_corrupt_object $blob_1 0 < zero &&
     test_must_fail git cat-file blob $blob_1 > /dev/null &&
     test_must_fail git cat-file blob $blob_2 > /dev/null &&
     test_must_fail git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    '... but having a loose copy allows for full recovery' \
    'mv ${pack}.idx tmp &&
     git hash-object -t blob -w file_1 &&
     mv tmp ${pack}.idx &&
     git cat-file blob $blob_1 > /dev/null &&
     git cat-file blob $blob_2 > /dev/null &&
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    '... and loose copy of first delta allows for partial recovery' \
    'git prune-packed &&
     test_must_fail git cat-file blob $blob_2 > /dev/null &&
     mv ${pack}.idx tmp &&
     git hash-object -t blob -w file_2 &&
     mv tmp ${pack}.idx &&
     test_must_fail git cat-file blob $blob_1 > /dev/null &&
     git cat-file blob $blob_2 > /dev/null &&
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    'create corruption in data of first object' \
    'create_new_pack &&
     git prune-packed &&
     chmod +w ${pack}.pack &&
     perl -i.bak -pe "s/ base /abcdef/" ${pack}.pack &&
     test_must_fail git cat-file blob $blob_1 > /dev/null &&
     test_must_fail git cat-file blob $blob_2 > /dev/null &&
     test_must_fail git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    '... but having a loose copy allows for full recovery' \
    'mv ${pack}.idx tmp &&
     git hash-object -t blob -w file_1 &&
     mv tmp ${pack}.idx &&
     git cat-file blob $blob_1 > /dev/null &&
     git cat-file blob $blob_2 > /dev/null &&
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    '... and loose copy of second object allows for partial recovery' \
    'git prune-packed &&
     test_must_fail git cat-file blob $blob_2 > /dev/null &&
     mv ${pack}.idx tmp &&
     git hash-object -t blob -w file_2 &&
     mv tmp ${pack}.idx &&
     test_must_fail git cat-file blob $blob_1 > /dev/null &&
     git cat-file blob $blob_2 > /dev/null &&
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    'create corruption in header of first delta' \
    'create_new_pack &&
     git prune-packed &&
     do_corrupt_object $blob_2 0 < zero &&
     git cat-file blob $blob_1 > /dev/null &&
     test_must_fail git cat-file blob $blob_2 > /dev/null &&
     test_must_fail git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    '... but having a loose copy allows for full recovery' \
    'mv ${pack}.idx tmp &&
     git hash-object -t blob -w file_2 &&
     mv tmp ${pack}.idx &&
     git cat-file blob $blob_1 > /dev/null &&
     git cat-file blob $blob_2 > /dev/null &&
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    '... and then a repack "clears" the corruption' \
    'do_repack &&
     git prune-packed &&
     git verify-pack ${pack}.pack &&
     git cat-file blob $blob_1 > /dev/null &&
     git cat-file blob $blob_2 > /dev/null &&
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    'create corruption in data of first delta' \
    'create_new_pack &&
     git prune-packed &&
     chmod +w ${pack}.pack &&
     perl -i.bak -pe "s/ delta1 /abcdefgh/" ${pack}.pack &&
     git cat-file blob $blob_1 > /dev/null &&
     test_must_fail git cat-file blob $blob_2 > /dev/null &&
     test_must_fail git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    '... but having a loose copy allows for full recovery' \
    'mv ${pack}.idx tmp &&
     git hash-object -t blob -w file_2 &&
     mv tmp ${pack}.idx &&
     git cat-file blob $blob_1 > /dev/null &&
     git cat-file blob $blob_2 > /dev/null &&
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    '... and then a repack "clears" the corruption' \
    'do_repack &&
     git prune-packed &&
     git verify-pack ${pack}.pack &&
     git cat-file blob $blob_1 > /dev/null &&
     git cat-file blob $blob_2 > /dev/null &&
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    'corruption in delta base reference of first delta (OBJ_REF_DELTA)' \
    'create_new_pack &&
     git prune-packed &&
     do_corrupt_object $blob_2 2 < zero &&
     git cat-file blob $blob_1 > /dev/null &&
     test_must_fail git cat-file blob $blob_2 > /dev/null &&
     test_must_fail git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    '... but having a loose copy allows for full recovery' \
    'mv ${pack}.idx tmp &&
     git hash-object -t blob -w file_2 &&
     mv tmp ${pack}.idx &&
     git cat-file blob $blob_1 > /dev/null &&
     git cat-file blob $blob_2 > /dev/null &&
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    '... and then a repack "clears" the corruption' \
    'do_repack &&
     git prune-packed &&
     git verify-pack ${pack}.pack &&
     git cat-file blob $blob_1 > /dev/null &&
     git cat-file blob $blob_2 > /dev/null &&
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    'corruption #0 in delta base reference of first delta (OBJ_OFS_DELTA)' \
    'create_new_pack --delta-base-offset &&
     git prune-packed &&
     do_corrupt_object $blob_2 2 < zero &&
     git cat-file blob $blob_1 > /dev/null &&
     test_must_fail git cat-file blob $blob_2 > /dev/null &&
     test_must_fail git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    '... but having a loose copy allows for full recovery' \
    'mv ${pack}.idx tmp &&
     git hash-object -t blob -w file_2 &&
     mv tmp ${pack}.idx &&
     git cat-file blob $blob_1 > /dev/null &&
     git cat-file blob $blob_2 > /dev/null &&
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    '... and then a repack "clears" the corruption' \
    'do_repack --delta-base-offset &&
     git prune-packed &&
     git verify-pack ${pack}.pack &&
     git cat-file blob $blob_1 > /dev/null &&
     git cat-file blob $blob_2 > /dev/null &&
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    'corruption #1 in delta base reference of first delta (OBJ_OFS_DELTA)' \
    'create_new_pack --delta-base-offset &&
     git prune-packed &&
     printf "\001" | do_corrupt_object $blob_2 2 &&
     git cat-file blob $blob_1 > /dev/null &&
     test_must_fail git cat-file blob $blob_2 > /dev/null &&
     test_must_fail git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    '... but having a loose copy allows for full recovery' \
    'mv ${pack}.idx tmp &&
     git hash-object -t blob -w file_2 &&
     mv tmp ${pack}.idx &&
     git cat-file blob $blob_1 > /dev/null &&
     git cat-file blob $blob_2 > /dev/null &&
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    '... and then a repack "clears" the corruption' \
    'do_repack --delta-base-offset &&
     git prune-packed &&
     git verify-pack ${pack}.pack &&
     git cat-file blob $blob_1 > /dev/null &&
     git cat-file blob $blob_2 > /dev/null &&
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    '... and a redundant pack allows for full recovery too' \
    'do_corrupt_object $blob_2 2 < zero &&
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
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    'corruption of delta base reference pointing to wrong object' \
    'create_new_pack --delta-base-offset &&
     git prune-packed &&
     printf "\220\033" | do_corrupt_object $blob_3 2 &&
     git cat-file blob $blob_1 >/dev/null &&
     git cat-file blob $blob_2 >/dev/null &&
     test_must_fail git cat-file blob $blob_3 >/dev/null'

test_expect_success \
    '... but having a loose copy allows for full recovery' \
    'mv ${pack}.idx tmp &&
     git hash-object -t blob -w file_3 &&
     mv tmp ${pack}.idx &&
     git cat-file blob $blob_1 > /dev/null &&
     git cat-file blob $blob_2 > /dev/null &&
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    '... and then a repack "clears" the corruption' \
    'do_repack --delta-base-offset --no-reuse-delta &&
     git prune-packed &&
     git verify-pack ${pack}.pack &&
     git cat-file blob $blob_1 > /dev/null &&
     git cat-file blob $blob_2 > /dev/null &&
     git cat-file blob $blob_3 > /dev/null'

test_expect_success \
    'corrupting header to have too small output buffer fails unpack' \
    'create_new_pack &&
     git prune-packed &&
     printf "\262\001" | do_corrupt_object $blob_1 0 &&
     test_must_fail git cat-file blob $blob_1 > /dev/null &&
     test_must_fail git cat-file blob $blob_2 > /dev/null &&
     test_must_fail git cat-file blob $blob_3 > /dev/null'

test_done
