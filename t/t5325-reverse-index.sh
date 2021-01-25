#!/bin/sh

test_description='on-disk reverse index'
. ./test-lib.sh

packdir=.git/objects/pack

test_expect_success 'setup' '
	test_commit base &&

	pack=$(git pack-objects --all $packdir/pack) &&
	rev=$packdir/pack-$pack.rev &&

	test_path_is_missing $rev
'

test_index_pack () {
	rm -f $rev &&
	conf=$1 &&
	shift &&
	# remove the index since Windows won't overwrite an existing file
	rm $packdir/pack-$pack.idx &&
	git -c pack.writeReverseIndex=$conf index-pack "$@" \
		$packdir/pack-$pack.pack
}

test_expect_success 'index-pack with pack.writeReverseIndex' '
	test_index_pack "" &&
	test_path_is_missing $rev &&

	test_index_pack false &&
	test_path_is_missing $rev &&

	test_index_pack true &&
	test_path_is_file $rev
'

test_expect_success 'index-pack with --[no-]rev-index' '
	for conf in "" true false
	do
		test_index_pack "$conf" --rev-index &&
		test_path_exists $rev &&

		test_index_pack "$conf" --no-rev-index &&
		test_path_is_missing $rev
	done
'

test_expect_success 'index-pack can verify reverse indexes' '
	test_when_finished "rm -f $rev" &&
	test_index_pack true &&

	test_path_is_file $rev &&
	git index-pack --rev-index --verify $packdir/pack-$pack.pack &&

	# Intentionally corrupt the reverse index.
	chmod u+w $rev &&
	printf "xxxx" | dd of=$rev bs=1 count=4 conv=notrunc &&

	test_must_fail git index-pack --rev-index --verify \
		$packdir/pack-$pack.pack 2>err &&
	grep "validation error" err
'

test_expect_success 'index-pack infers reverse index name with -o' '
	git index-pack --rev-index -o other.idx $packdir/pack-$pack.pack &&
	test_path_is_file other.idx &&
	test_path_is_file other.rev
'

test_done
