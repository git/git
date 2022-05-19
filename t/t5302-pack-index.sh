#!/bin/sh
#
# Copyright (c) 2007 Nicolas Pitre
#

test_description='pack index with 64-bit offsets and object CRC'
. ./test-lib.sh

test_expect_success 'setup' '
	rawsz=$(test_oid rawsz) &&
	rm -rf .but &&
	but init &&
	but config pack.threads 1 &&
	i=1 &&
	while test $i -le 100
	do
		iii=$(printf "%03i" $i) &&
		test-tool genrandom "bar" 200 > wide_delta_$iii &&
		test-tool genrandom "baz $iii" 50 >> wide_delta_$iii &&
		test-tool genrandom "foo"$i 100 > deep_delta_$iii &&
		test-tool genrandom "foo"$(expr $i + 1) 100 >> deep_delta_$iii &&
		test-tool genrandom "foo"$(expr $i + 2) 100 >> deep_delta_$iii &&
		echo $iii >file_$iii &&
		test-tool genrandom "$iii" 8192 >>file_$iii &&
		but update-index --add file_$iii deep_delta_$iii wide_delta_$iii &&
		i=$(expr $i + 1) || return 1
	done &&
	{ echo 101 && test-tool genrandom 100 8192; } >file_101 &&
	but update-index --add file_101 &&
	tree=$(but write-tree) &&
	cummit=$(but cummit-tree $tree </dev/null) && {
		echo $tree &&
		but ls-tree $tree | sed -e "s/.* \\([0-9a-f]*\\)	.*/\\1/"
	} >obj-list &&
	but update-ref HEAD $cummit
'

test_expect_success 'pack-objects with index version 1' '
	pack1=$(but pack-objects --index-version=1 test-1 <obj-list) &&
	but verify-pack -v "test-1-${pack1}.pack"
'

test_expect_success 'pack-objects with index version 2' '
	pack2=$(but pack-objects --index-version=2 test-2 <obj-list) &&
	but verify-pack -v "test-2-${pack2}.pack"
'

test_expect_success 'both packs should be identical' '
	cmp "test-1-${pack1}.pack" "test-2-${pack2}.pack"
'

test_expect_success 'index v1 and index v2 should be different' '
	! cmp "test-1-${pack1}.idx" "test-2-${pack2}.idx"
'

test_expect_success 'index-pack with index version 1' '
	but index-pack --index-version=1 -o 1.idx "test-1-${pack1}.pack"
'

test_expect_success 'index-pack with index version 2' '
	but index-pack --index-version=2 -o 2.idx "test-1-${pack1}.pack"
'

test_expect_success 'index-pack results should match pack-objects ones' '
	cmp "test-1-${pack1}.idx" "1.idx" &&
	cmp "test-2-${pack2}.idx" "2.idx"
'

test_expect_success 'index-pack --verify on index version 1' '
	but index-pack --verify "test-1-${pack1}.pack"
'

test_expect_success 'index-pack --verify on index version 2' '
	but index-pack --verify "test-2-${pack2}.pack"
'

test_expect_success 'pack-objects --index-version=2, is not accepted' '
	test_must_fail but pack-objects --index-version=2, test-3 <obj-list
'

test_expect_success 'index v2: force some 64-bit offsets with pack-objects' '
	pack3=$(but pack-objects --index-version=2,0x40000 test-3 <obj-list)
'

if msg=$(but verify-pack -v "test-3-${pack3}.pack" 2>&1) ||
	! (echo "$msg" | grep "pack too large .* off_t")
then
	test_set_prereq OFF64_T
else
	say "# skipping tests concerning 64-bit offsets"
fi

test_expect_success OFF64_T 'index v2: verify a pack with some 64-bit offsets' '
	but verify-pack -v "test-3-${pack3}.pack"
'

test_expect_success OFF64_T '64-bit offsets: should be different from previous index v2 results' '
	! cmp "test-2-${pack2}.idx" "test-3-${pack3}.idx"
'

test_expect_success OFF64_T 'index v2: force some 64-bit offsets with index-pack' '
	but index-pack --index-version=2,0x40000 -o 3.idx "test-1-${pack1}.pack"
'

test_expect_success OFF64_T '64-bit offsets: index-pack result should match pack-objects one' '
	cmp "test-3-${pack3}.idx" "3.idx"
'

test_expect_success OFF64_T 'index-pack --verify on 64-bit offset v2 (cheat)' '
	# This cheats by knowing which lower offset should still be encoded
	# in 64-bit representation.
	but index-pack --verify --index-version=2,0x40000 "test-3-${pack3}.pack"
'

test_expect_success OFF64_T 'index-pack --verify on 64-bit offset v2' '
	but index-pack --verify "test-3-${pack3}.pack"
'

# returns the object number for given object in given pack index
index_obj_nr()
{
	idx_file=$1
	object_sha1=$2
	nr=0
	but show-index < $idx_file |
	while read offs sha1 extra
	do
	  nr=$(($nr + 1))
	  test "$sha1" = "$object_sha1" || continue
	  echo "$(($nr - 1))"
	  break
	done
}

# returns the pack offset for given object as found in given pack index
index_obj_offset()
{
	idx_file=$1
	object_sha1=$2
	but show-index < $idx_file | grep $object_sha1 |
	( read offs extra && echo "$offs" )
}

test_expect_success '[index v1] 1) stream pack to repository' '
	but index-pack --index-version=1 --stdin < "test-1-${pack1}.pack" &&
	but prune-packed &&
	but count-objects | ( read nr rest && test "$nr" -eq 1 ) &&
	cmp "test-1-${pack1}.pack" ".but/objects/pack/pack-${pack1}.pack" &&
	cmp "test-1-${pack1}.idx"	".but/objects/pack/pack-${pack1}.idx"
'

test_expect_success \
	'[index v1] 2) create a stealth corruption in a delta base reference' '
	# This test assumes file_101 is a delta smaller than 16 bytes.
	# It should be against file_100 but we substitute its base for file_099
	sha1_101=$(but hash-object file_101) &&
	sha1_099=$(but hash-object file_099) &&
	offs_101=$(index_obj_offset 1.idx $sha1_101) &&
	nr_099=$(index_obj_nr 1.idx $sha1_099) &&
	chmod +w ".but/objects/pack/pack-${pack1}.pack" &&
	recordsz=$((rawsz + 4)) &&
	dd of=".but/objects/pack/pack-${pack1}.pack" seek=$(($offs_101 + 1)) \
	       if=".but/objects/pack/pack-${pack1}.idx" \
	       skip=$((4 + 256 * 4 + $nr_099 * recordsz)) \
	       bs=1 count=$rawsz conv=notrunc &&
	but cat-file blob $sha1_101 > file_101_foo1
'

test_expect_success \
	'[index v1] 3) corrupted delta happily returned wrong data' '
	test -f file_101_foo1 && ! cmp file_101 file_101_foo1
'

test_expect_success \
	'[index v1] 4) confirm that the pack is actually corrupted' '
	test_must_fail but fsck --full $cummit
'

test_expect_success \
	'[index v1] 5) pack-objects happily reuses corrupted data' '
	pack4=$(but pack-objects test-4 <obj-list) &&
	test -f "test-4-${pack4}.pack"
'

test_expect_success '[index v1] 6) newly created pack is BAD !' '
	test_must_fail but verify-pack -v "test-4-${pack4}.pack"
'

test_expect_success '[index v2] 1) stream pack to repository' '
	rm -f .but/objects/pack/* &&
	but index-pack --index-version=2 --stdin < "test-1-${pack1}.pack" &&
	but prune-packed &&
	but count-objects | ( read nr rest && test "$nr" -eq 1 ) &&
	cmp "test-1-${pack1}.pack" ".but/objects/pack/pack-${pack1}.pack" &&
	cmp "test-2-${pack1}.idx"	".but/objects/pack/pack-${pack1}.idx"
'

test_expect_success \
	'[index v2] 2) create a stealth corruption in a delta base reference' '
	# This test assumes file_101 is a delta smaller than 16 bytes.
	# It should be against file_100 but we substitute its base for file_099
	sha1_101=$(but hash-object file_101) &&
	sha1_099=$(but hash-object file_099) &&
	offs_101=$(index_obj_offset 1.idx $sha1_101) &&
	nr_099=$(index_obj_nr 1.idx $sha1_099) &&
	chmod +w ".but/objects/pack/pack-${pack1}.pack" &&
	dd of=".but/objects/pack/pack-${pack1}.pack" seek=$(($offs_101 + 1)) \
		if=".but/objects/pack/pack-${pack1}.idx" \
		skip=$((8 + 256 * 4 + $nr_099 * rawsz)) \
		bs=1 count=$rawsz conv=notrunc &&
	but cat-file blob $sha1_101 > file_101_foo2
'

test_expect_success \
	'[index v2] 3) corrupted delta happily returned wrong data' '
	test -f file_101_foo2 && ! cmp file_101 file_101_foo2
'

test_expect_success \
	'[index v2] 4) confirm that the pack is actually corrupted' '
	test_must_fail but fsck --full $cummit
'

test_expect_success \
	'[index v2] 5) pack-objects refuses to reuse corrupted data' '
	test_must_fail but pack-objects test-5 <obj-list &&
	test_must_fail but pack-objects --no-reuse-object test-6 <obj-list
'

test_expect_success \
	'[index v2] 6) verify-pack detects CRC mismatch' '
	rm -f .but/objects/pack/* &&
	but index-pack --index-version=2 --stdin < "test-1-${pack1}.pack" &&
	but verify-pack ".but/objects/pack/pack-${pack1}.pack" &&
	obj=$(but hash-object file_001) &&
	nr=$(index_obj_nr ".but/objects/pack/pack-${pack1}.idx" $obj) &&
	chmod +w ".but/objects/pack/pack-${pack1}.idx" &&
	printf xxxx | dd of=".but/objects/pack/pack-${pack1}.idx" conv=notrunc \
		bs=1 count=4 seek=$((8 + 256 * 4 + $(wc -l <obj-list) * rawsz + $nr * 4)) &&
	 ( while read obj
	   do but cat-file -p $obj >/dev/null || exit 1
	   done <obj-list ) &&
	test_must_fail but verify-pack ".but/objects/pack/pack-${pack1}.pack"
'

test_expect_success 'running index-pack in the object store' '
	rm -f .but/objects/pack/* &&
	cp test-1-${pack1}.pack .but/objects/pack/pack-${pack1}.pack &&
	(
		cd .but/objects/pack &&
		but index-pack pack-${pack1}.pack
	) &&
	test -f .but/objects/pack/pack-${pack1}.idx
'

test_expect_success 'index-pack --strict warns upon missing tagger in tag' '
	sha=$(but rev-parse HEAD) &&
	cat >wrong-tag <<EOF &&
object $sha
type cummit
tag guten tag

This is an invalid tag.
EOF

	tag=$(but hash-object -t tag -w --stdin <wrong-tag) &&
	pack1=$(echo $tag $sha | but pack-objects tag-test) &&
	echo remove tag object &&
	thirtyeight=${tag#??} &&
	rm -f .but/objects/${tag%$thirtyeight}/$thirtyeight &&
	but index-pack --strict tag-test-${pack1}.pack 2>err &&
	grep "^warning:.* expected .tagger. line" err
'

test_expect_success 'index-pack --fsck-objects also warns upon missing tagger in tag' '
	but index-pack --fsck-objects tag-test-${pack1}.pack 2>err &&
	grep "^warning:.* expected .tagger. line" err
'

test_expect_success 'index-pack -v --stdin produces progress for both phases' '
	pack=$(but pack-objects --all pack </dev/null) &&
	BUT_PROGRESS_DELAY=0 but index-pack -v --stdin <pack-$pack.pack 2>err &&
	test_i18ngrep "Receiving objects" err &&
	test_i18ngrep "Resolving deltas" err
'

test_expect_success 'too-large packs report the breach' '
	pack=$(but pack-objects --all pack </dev/null) &&
	sz="$(test_file_size pack-$pack.pack)" &&
	test "$sz" -gt 20 &&
	test_must_fail but index-pack --max-input-size=20 pack-$pack.pack 2>err &&
	grep "maximum allowed size (20 bytes)" err
'

test_done
