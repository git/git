#!/bin/sh

test_description='bounds-checking of access to mmapped on-disk file formats'

. ./test-lib.sh

clear_base () {
	test_when_finished 'restore_base' &&
	rm -r -f $base
}

restore_base () {
	cp -r base-backup/* .git/objects/pack/
}

do_pack () {
	pack_objects=$1; shift
	sha1=$(
		for i in $pack_objects
		do
			echo $i
		done | git pack-objects "$@" .git/objects/pack/pack
	) &&
	pack=.git/objects/pack/pack-$sha1.pack &&
	idx=.git/objects/pack/pack-$sha1.idx &&
	chmod +w $pack $idx &&
	test_when_finished 'rm -f "$pack" "$idx"'
}

munge () {
	printf "$3" | dd of="$1" bs=1 conv=notrunc seek=$2
}

# Offset in a v2 .idx to its initial and extended offset tables. For an index
# with "nr" objects, this is:
#
#   magic(4) + version(4) + fan-out(4*256) + sha1s(20*nr) + crc(4*nr),
#
# for the initial, and another ofs(4*nr) past that for the extended.
#
ofs_table () {
	echo $((4 + 4 + 4*256 + $(test_oid rawsz)*$1 + 4*$1))
}
extended_table () {
	echo $(($(ofs_table "$1") + 4*$1))
}

test_expect_success 'setup' '
	test_oid_cache <<-EOF
	oid000 sha1:1485
	oid000 sha256:4222

	oidfff sha1:74
	oidfff sha256:1350
	EOF
'

test_expect_success 'set up base packfile and variables' '
	# the hash of this content starts with ff, which
	# makes some later computations much simpler
	test_oid oidfff >file &&
	git add file &&
	git commit -m base &&
	git repack -ad &&
	base=$(echo .git/objects/pack/*) &&
	chmod -R +w $base &&
	mkdir base-backup &&
	cp -r $base base-backup/ &&
	object=$(git rev-parse HEAD:file)
'

test_expect_success 'pack/index object count mismatch' '
	do_pack $object &&
	munge $pack 8 "\377\0\0\0" &&
	clear_base &&

	# We enumerate the objects from the completely-fine
	# .idx, but notice later that the .pack is bogus
	# and fail to show any data.
	echo "$object missing" >expect &&
	git cat-file --batch-all-objects --batch-check >actual &&
	test_cmp expect actual &&

	# ...and here fail to load the object (without segfaulting),
	# but fallback to a good copy if available.
	test_must_fail git cat-file blob $object &&
	restore_base &&
	git cat-file blob $object >actual &&
	test_cmp file actual &&

	# ...and make sure that index-pack --verify, which has its
	# own reading routines, does not segfault.
	test_must_fail git index-pack --verify $pack
'

test_expect_success 'matched bogus object count' '
	do_pack $object &&
	munge $pack 8 "\377\0\0\0" &&
	munge $idx $((255 * 4)) "\377\0\0\0" &&
	clear_base &&

	# Unlike above, we should notice early that the .idx is totally
	# bogus, and not even enumerate its contents.
	git cat-file --batch-all-objects --batch-check >actual &&
	test_must_be_empty actual &&

	# But as before, we can do the same object-access checks.
	test_must_fail git cat-file blob $object &&
	restore_base &&
	git cat-file blob $object >actual &&
	test_cmp file actual &&

	test_must_fail git index-pack --verify $pack
'

# Note that we cannot check the fallback case for these
# further .idx tests, as we notice the problem in functions
# whose interface doesn't allow an error return (like use_pack()),
# and thus we just die().
#
# There's also no point in doing enumeration tests, as
# we are munging offsets here, which are about looking up
# specific objects.

test_expect_success 'bogus object offset (v1)' '
	do_pack $object --index-version=1 &&
	munge $idx $((4 * 256)) "\377\0\0\0" &&
	clear_base &&
	test_must_fail git cat-file blob $object &&
	test_must_fail git index-pack --verify $pack
'

test_expect_success 'bogus object offset (v2, no msb)' '
	do_pack $object --index-version=2 &&
	munge $idx $(ofs_table 1) "\0\377\0\0" &&
	clear_base &&
	test_must_fail git cat-file blob $object &&
	test_must_fail git index-pack --verify $pack
'

test_expect_success 'bogus offset into v2 extended table' '
	do_pack $object --index-version=2 &&
	munge $idx $(ofs_table 1) "\377\0\0\0" &&
	clear_base &&
	test_must_fail git cat-file blob $object &&
	test_must_fail git index-pack --verify $pack
'

test_expect_success 'bogus offset inside v2 extended table' '
	# We need two objects here, so we can plausibly require
	# an extended table (if the first object were larger than 2^31).
	#
	# Note that the value is important here. We want $object as
	# the second entry in sorted-hash order. The hash of this object starts
	# with "000", which sorts before that of $object (which starts
	# with "fff").
	second=$(test_oid oid000 | git hash-object -w --stdin) &&
	do_pack "$object $second" --index-version=2 &&

	# We have to make extra room for the table, so we cannot
	# just munge in place as usual.
	{
		dd if=$idx bs=1 count=$(($(ofs_table 2) + 4)) &&
		printf "\200\0\0\0" &&
		printf "\377\0\0\0\0\0\0\0" &&
		dd if=$idx bs=1 skip=$(extended_table 2)
	} >tmp &&
	mv tmp "$idx" &&
	clear_base &&
	test_must_fail git cat-file blob $object &&
	test_must_fail git index-pack --verify $pack
'

test_expect_success 'bogus OFS_DELTA in packfile' '
	# Generate a pack with a delta in it.
	base=$(test-tool genrandom foo 3000 | git hash-object --stdin -w) &&
	delta=$(test-tool genrandom foo 2000 | git hash-object --stdin -w) &&
	do_pack "$base $delta" --delta-base-offset &&
	rm -f .git/objects/??/* &&

	# Double check that we have the delta we expect.
	echo $base >expect &&
	echo $delta | git cat-file --batch-check="%(deltabase)" >actual &&
	test_cmp expect actual &&

	# Now corrupt it. We assume the varint size for the delta is small
	# enough to fit in the first byte (which it should be, since it
	# is a pure deletion from the base), and that original ofs_delta
	# takes 2 bytes (which it should, as it should be ~3000).
	ofs=$(git show-index <$idx | grep $delta | cut -d" " -f1) &&
	munge $pack $(($ofs + 1)) "\177\377" &&
	test_must_fail git cat-file blob $delta >/dev/null
'

test_done
