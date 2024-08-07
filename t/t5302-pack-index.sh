#!/bin/sh
#
# Copyright (c) 2007 Nicolas Pitre
#

test_description='pack index with 64-bit offsets and object CRC'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	rawsz=$(test_oid rawsz) &&
	rm -rf .git &&
	git init &&
	git config pack.threads 1 &&
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
		git update-index --add file_$iii deep_delta_$iii wide_delta_$iii &&
		i=$(expr $i + 1) || return 1
	done &&
	{ echo 101 && test-tool genrandom 100 8192; } >file_101 &&
	git update-index --add file_101 &&
	tree=$(git write-tree) &&
	commit=$(git commit-tree $tree </dev/null) && {
		echo $tree &&
		git ls-tree $tree | sed -e "s/.* \\([0-9a-f]*\\)	.*/\\1/"
	} >obj-list &&
	git update-ref HEAD $commit
'

test_expect_success 'pack-objects with index version 1' '
	pack1=$(git pack-objects --index-version=1 test-1 <obj-list) &&
	git verify-pack -v "test-1-${pack1}.pack"
'

test_expect_success 'pack-objects with index version 2' '
	pack2=$(git pack-objects --index-version=2 test-2 <obj-list) &&
	git verify-pack -v "test-2-${pack2}.pack"
'

test_expect_success 'both packs should be identical' '
	cmp "test-1-${pack1}.pack" "test-2-${pack2}.pack"
'

test_expect_success 'index v1 and index v2 should be different' '
	! cmp "test-1-${pack1}.idx" "test-2-${pack2}.idx"
'

test_expect_success 'index-pack with index version 1' '
	git index-pack --index-version=1 -o 1.idx "test-1-${pack1}.pack"
'

test_expect_success 'index-pack with index version 2' '
	git index-pack --index-version=2 -o 2.idx "test-1-${pack1}.pack"
'

test_expect_success 'index-pack results should match pack-objects ones' '
	cmp "test-1-${pack1}.idx" "1.idx" &&
	cmp "test-2-${pack2}.idx" "2.idx"
'

test_expect_success 'index-pack --verify on index version 1' '
	git index-pack --verify "test-1-${pack1}.pack"
'

test_expect_success 'index-pack --verify on index version 2' '
	git index-pack --verify "test-2-${pack2}.pack"
'

test_expect_success 'pack-objects --index-version=2, is not accepted' '
	test_must_fail git pack-objects --index-version=2, test-3 <obj-list
'

test_expect_success 'index v2: force some 64-bit offsets with pack-objects' '
	pack3=$(git pack-objects --index-version=2,0x40000 test-3 <obj-list)
'

if msg=$(git verify-pack -v "test-3-${pack3}.pack" 2>&1) ||
	! (echo "$msg" | grep "pack too large .* off_t")
then
	test_set_prereq OFF64_T
else
	say "# skipping tests concerning 64-bit offsets"
fi

test_expect_success OFF64_T 'index v2: verify a pack with some 64-bit offsets' '
	git verify-pack -v "test-3-${pack3}.pack"
'

test_expect_success OFF64_T '64-bit offsets: should be different from previous index v2 results' '
	! cmp "test-2-${pack2}.idx" "test-3-${pack3}.idx"
'

test_expect_success OFF64_T 'index v2: force some 64-bit offsets with index-pack' '
	git index-pack --index-version=2,0x40000 -o 3.idx "test-1-${pack1}.pack"
'

test_expect_success OFF64_T '64-bit offsets: index-pack result should match pack-objects one' '
	cmp "test-3-${pack3}.idx" "3.idx"
'

test_expect_success OFF64_T 'index-pack --verify on 64-bit offset v2 (cheat)' '
	# This cheats by knowing which lower offset should still be encoded
	# in 64-bit representation.
	git index-pack --verify --index-version=2,0x40000 "test-3-${pack3}.pack"
'

test_expect_success OFF64_T 'index-pack --verify on 64-bit offset v2' '
	git index-pack --verify "test-3-${pack3}.pack"
'

# returns the object number for given object in given pack index
index_obj_nr()
{
	idx_file=$1
	object_sha1=$2
	nr=0
	git show-index < $idx_file |
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
	git show-index < $idx_file | grep $object_sha1 |
	( read offs extra && echo "$offs" )
}

test_expect_success '[index v1] 1) stream pack to repository' '
	git index-pack --index-version=1 --stdin < "test-1-${pack1}.pack" &&
	git prune-packed &&
	git count-objects | ( read nr rest && test "$nr" -eq 1 ) &&
	cmp "test-1-${pack1}.pack" ".git/objects/pack/pack-${pack1}.pack" &&
	cmp "test-1-${pack1}.idx"	".git/objects/pack/pack-${pack1}.idx"
'

test_expect_success \
	'[index v1] 2) create a stealth corruption in a delta base reference' '
	# This test assumes file_101 is a delta smaller than 16 bytes.
	# It should be against file_100 but we substitute its base for file_099
	sha1_101=$(git hash-object file_101) &&
	sha1_099=$(git hash-object file_099) &&
	offs_101=$(index_obj_offset 1.idx $sha1_101) &&
	nr_099=$(index_obj_nr 1.idx $sha1_099) &&
	chmod +w ".git/objects/pack/pack-${pack1}.pack" &&
	recordsz=$((rawsz + 4)) &&
	dd of=".git/objects/pack/pack-${pack1}.pack" seek=$(($offs_101 + 1)) \
	       if=".git/objects/pack/pack-${pack1}.idx" \
	       skip=$((4 + 256 * 4 + $nr_099 * recordsz)) \
	       bs=1 count=$rawsz conv=notrunc &&
	git cat-file blob $sha1_101 > file_101_foo1
'

test_expect_success \
	'[index v1] 3) corrupted delta happily returned wrong data' '
	test -f file_101_foo1 && ! cmp file_101 file_101_foo1
'

test_expect_success \
	'[index v1] 4) confirm that the pack is actually corrupted' '
	test_must_fail git fsck --full $commit
'

test_expect_success \
	'[index v1] 5) pack-objects happily reuses corrupted data' '
	pack4=$(git pack-objects test-4 <obj-list) &&
	test -f "test-4-${pack4}.pack"
'

test_expect_success '[index v1] 6) newly created pack is BAD !' '
	test_must_fail git verify-pack -v "test-4-${pack4}.pack"
'

test_expect_success '[index v2] 1) stream pack to repository' '
	rm -f .git/objects/pack/* &&
	git index-pack --index-version=2 --stdin < "test-1-${pack1}.pack" &&
	git prune-packed &&
	git count-objects | ( read nr rest && test "$nr" -eq 1 ) &&
	cmp "test-1-${pack1}.pack" ".git/objects/pack/pack-${pack1}.pack" &&
	cmp "test-2-${pack1}.idx"	".git/objects/pack/pack-${pack1}.idx"
'

test_expect_success \
	'[index v2] 2) create a stealth corruption in a delta base reference' '
	# This test assumes file_101 is a delta smaller than 16 bytes.
	# It should be against file_100 but we substitute its base for file_099
	sha1_101=$(git hash-object file_101) &&
	sha1_099=$(git hash-object file_099) &&
	offs_101=$(index_obj_offset 1.idx $sha1_101) &&
	nr_099=$(index_obj_nr 1.idx $sha1_099) &&
	chmod +w ".git/objects/pack/pack-${pack1}.pack" &&
	dd of=".git/objects/pack/pack-${pack1}.pack" seek=$(($offs_101 + 1)) \
		if=".git/objects/pack/pack-${pack1}.idx" \
		skip=$((8 + 256 * 4 + $nr_099 * rawsz)) \
		bs=1 count=$rawsz conv=notrunc &&
	git cat-file blob $sha1_101 > file_101_foo2
'

test_expect_success \
	'[index v2] 3) corrupted delta happily returned wrong data' '
	test -f file_101_foo2 && ! cmp file_101 file_101_foo2
'

test_expect_success \
	'[index v2] 4) confirm that the pack is actually corrupted' '
	test_must_fail git fsck --full $commit
'

test_expect_success \
	'[index v2] 5) pack-objects refuses to reuse corrupted data' '
	test_must_fail git pack-objects test-5 <obj-list &&
	test_must_fail git pack-objects --no-reuse-object test-6 <obj-list
'

test_expect_success \
	'[index v2] 6) verify-pack detects CRC mismatch' '
	rm -f .git/objects/pack/* &&
	git index-pack --index-version=2 --stdin < "test-1-${pack1}.pack" &&
	git verify-pack ".git/objects/pack/pack-${pack1}.pack" &&
	obj=$(git hash-object file_001) &&
	nr=$(index_obj_nr ".git/objects/pack/pack-${pack1}.idx" $obj) &&
	chmod +w ".git/objects/pack/pack-${pack1}.idx" &&
	printf xxxx | dd of=".git/objects/pack/pack-${pack1}.idx" conv=notrunc \
		bs=1 count=4 seek=$((8 + 256 * 4 + $(wc -l <obj-list) * rawsz + $nr * 4)) &&
	 ( while read obj
	   do git cat-file -p $obj >/dev/null || exit 1
	   done <obj-list ) &&
	test_must_fail git verify-pack ".git/objects/pack/pack-${pack1}.pack"
'

test_expect_success 'running index-pack in the object store' '
	rm -f .git/objects/pack/* &&
	cp test-1-${pack1}.pack .git/objects/pack/pack-${pack1}.pack &&
	(
		cd .git/objects/pack &&
		git index-pack pack-${pack1}.pack
	) &&
	test -f .git/objects/pack/pack-${pack1}.idx
'

test_expect_success 'index-pack --strict warns upon missing tagger in tag' '
	sha=$(git rev-parse HEAD) &&
	cat >wrong-tag <<EOF &&
object $sha
type commit
tag guten tag

This is an invalid tag.
EOF

	tag=$(git hash-object -t tag -w --stdin --literally <wrong-tag) &&
	pack1=$(echo $tag $sha | git pack-objects tag-test) &&
	echo remove tag object &&
	thirtyeight=${tag#??} &&
	rm -f .git/objects/${tag%$thirtyeight}/$thirtyeight &&
	git index-pack --strict tag-test-${pack1}.pack 2>err &&
	grep "^warning:.* expected .tagger. line" err
'

test_expect_success 'index-pack --fsck-objects also warns upon missing tagger in tag' '
	git index-pack --fsck-objects tag-test-${pack1}.pack 2>err &&
	grep "^warning:.* expected .tagger. line" err
'

test_expect_success 'index-pack -v --stdin produces progress for both phases' '
	pack=$(git pack-objects --all pack </dev/null) &&
	GIT_PROGRESS_DELAY=0 git index-pack -v --stdin <pack-$pack.pack 2>err &&
	test_grep "Receiving objects" err &&
	test_grep "Resolving deltas" err
'

test_expect_success 'too-large packs report the breach' '
	pack=$(git pack-objects --all pack </dev/null) &&
	sz="$(test_file_size pack-$pack.pack)" &&
	test "$sz" -gt 20 &&
	test_must_fail git index-pack --max-input-size=20 pack-$pack.pack 2>err &&
	grep "maximum allowed size (20 bytes)" err
'

test_done
