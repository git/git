#!/bin/sh
#
# Copyright (c) 2007 Nicolas Pitre
#

test_description='pack index with 64-bit offsets and object CRC'
. ./test-lib.sh

test_expect_success \
    'setup' \
    'rm -rf .git
     git init &&
     i=1 &&
	 while test $i -le 100
     do
		 i=`printf '%03i' $i`
         echo $i >file_$i &&
         test-genrandom "$i" 8192 >>file_$i &&
         git update-index --add file_$i &&
		 i=`expr $i + 1` || return 1
     done &&
     { echo 101 && test-genrandom 100 8192; } >file_101 &&
     git update-index --add file_101 &&
     tree=`git write-tree` &&
     commit=`git commit-tree $tree </dev/null` && {
	 echo $tree &&
	 git ls-tree $tree | sed -e "s/.* \\([0-9a-f]*\\)	.*/\\1/"
     } >obj-list &&
     git update-ref HEAD $commit'

test_expect_success \
    'pack-objects with index version 1' \
    'pack1=$(git pack-objects --index-version=1 test-1 <obj-list) &&
     git verify-pack -v "test-1-${pack1}.pack"'

test_expect_success \
    'pack-objects with index version 2' \
    'pack2=$(git pack-objects --index-version=2 test-2 <obj-list) &&
     git verify-pack -v "test-2-${pack2}.pack"'

test_expect_success \
    'both packs should be identical' \
    'cmp "test-1-${pack1}.pack" "test-2-${pack2}.pack"'

test_expect_failure \
    'index v1 and index v2 should be different' \
    'cmp "test-1-${pack1}.idx" "test-2-${pack2}.idx"'

test_expect_success \
    'index-pack with index version 1' \
    'git-index-pack --index-version=1 -o 1.idx "test-1-${pack1}.pack"'

test_expect_success \
    'index-pack with index version 2' \
    'git-index-pack --index-version=2 -o 2.idx "test-1-${pack1}.pack"'

test_expect_success \
    'index-pack results should match pack-objects ones' \
    'cmp "test-1-${pack1}.idx" "1.idx" &&
     cmp "test-2-${pack2}.idx" "2.idx"'

test_expect_success \
    'index v2: force some 64-bit offsets with pack-objects' \
    'pack3=$(git pack-objects --index-version=2,0x40000 test-3 <obj-list)'

have_64bits=
if msg=$(git verify-pack -v "test-3-${pack3}.pack" 2>&1) ||
	! echo "$msg" | grep "pack too large .* off_t"
then
	have_64bits=t
else
	say "skipping tests concerning 64-bit offsets"
fi

test "$have_64bits" &&
test_expect_success \
    'index v2: verify a pack with some 64-bit offsets' \
    'git verify-pack -v "test-3-${pack3}.pack"'

test "$have_64bits" &&
test_expect_failure \
    '64-bit offsets: should be different from previous index v2 results' \
    'cmp "test-2-${pack2}.idx" "test-3-${pack3}.idx"'

test "$have_64bits" &&
test_expect_success \
    'index v2: force some 64-bit offsets with index-pack' \
    'git-index-pack --index-version=2,0x40000 -o 3.idx "test-1-${pack1}.pack"'

test "$have_64bits" &&
test_expect_success \
    '64-bit offsets: index-pack result should match pack-objects one' \
    'cmp "test-3-${pack3}.idx" "3.idx"'

test_expect_success \
    '[index v1] 1) stream pack to repository' \
    'git-index-pack --index-version=1 --stdin < "test-1-${pack1}.pack" &&
     git prune-packed &&
     git count-objects | ( read nr rest && test "$nr" -eq 1 ) &&
     cmp "test-1-${pack1}.pack" ".git/objects/pack/pack-${pack1}.pack" &&
     cmp "test-1-${pack1}.idx"  ".git/objects/pack/pack-${pack1}.idx"'

test_expect_success \
    '[index v1] 2) create a stealth corruption in a delta base reference' \
    '# this test assumes a delta smaller than 16 bytes at the end of the pack
     git show-index <1.idx | sort -n | tail -n 1 | (
       read delta_offs delta_sha1 &&
       git cat-file blob "$delta_sha1" > blob_1 &&
       chmod +w ".git/objects/pack/pack-${pack1}.pack" &&
       dd of=".git/objects/pack/pack-${pack1}.pack" seek=$(($delta_offs + 1)) \
	  if=".git/objects/pack/pack-${pack1}.idx" skip=$((256 * 4 + 4)) \
	  bs=1 count=20 conv=notrunc &&
       git cat-file blob "$delta_sha1" > blob_2 )'

test_expect_failure \
    '[index v1] 3) corrupted delta happily returned wrong data' \
    'cmp blob_1 blob_2'

test_expect_failure \
    '[index v1] 4) confirm that the pack is actually corrupted' \
    'git fsck --full $commit'

test_expect_success \
    '[index v1] 5) pack-objects happily reuses corrupted data' \
    'pack4=$(git pack-objects test-4 <obj-list) &&
     test -f "test-4-${pack1}.pack"'

test_expect_failure \
    '[index v1] 6) newly created pack is BAD !' \
    'git verify-pack -v "test-4-${pack1}.pack"'

test_expect_success \
    '[index v2] 1) stream pack to repository' \
    'rm -f .git/objects/pack/* &&
     git-index-pack --index-version=2 --stdin < "test-1-${pack1}.pack" &&
     git prune-packed &&
     git count-objects | ( read nr rest && test "$nr" -eq 1 ) &&
     cmp "test-1-${pack1}.pack" ".git/objects/pack/pack-${pack1}.pack" &&
     cmp "test-2-${pack1}.idx"  ".git/objects/pack/pack-${pack1}.idx"'

test_expect_success \
    '[index v2] 2) create a stealth corruption in a delta base reference' \
    '# this test assumes a delta smaller than 16 bytes at the end of the pack
     git show-index <1.idx | sort -n | tail -n 1 | (
       read delta_offs delta_sha1 delta_crc &&
       git cat-file blob "$delta_sha1" > blob_3 &&
       chmod +w ".git/objects/pack/pack-${pack1}.pack" &&
       dd of=".git/objects/pack/pack-${pack1}.pack" seek=$(($delta_offs + 1)) \
	  if=".git/objects/pack/pack-${pack1}.idx" skip=$((8 + 256 * 4)) \
	  bs=1 count=20 conv=notrunc &&
       git cat-file blob "$delta_sha1" > blob_4 )'

test_expect_failure \
    '[index v2] 3) corrupted delta happily returned wrong data' \
    'cmp blob_3 blob_4'

test_expect_failure \
    '[index v2] 4) confirm that the pack is actually corrupted' \
    'git fsck --full $commit'

test_expect_failure \
    '[index v2] 5) pack-objects refuses to reuse corrupted data' \
    'git pack-objects test-5 <obj-list'

test_done
