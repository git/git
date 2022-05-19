#!/bin/sh

test_description='on-disk reverse index'
. ./test-lib.sh

# The below tests want control over the 'pack.writeReverseIndex' setting
# themselves to assert various combinations of it with other options.
sane_unset BUT_TEST_WRITE_REV_INDEX

packdir=.but/objects/pack

test_expect_success 'setup' '
	test_cummit base &&

	pack=$(but pack-objects --all $packdir/pack) &&
	rev=$packdir/pack-$pack.rev &&

	test_path_is_missing $rev
'

test_index_pack () {
	rm -f $rev &&
	conf=$1 &&
	shift &&
	# remove the index since Windows won't overwrite an existing file
	rm $packdir/pack-$pack.idx &&
	but -c pack.writeReverseIndex=$conf index-pack "$@" \
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
		test_path_is_missing $rev || return 1
	done
'

test_expect_success 'index-pack can verify reverse indexes' '
	test_when_finished "rm -f $rev" &&
	test_index_pack true &&

	test_path_is_file $rev &&
	but index-pack --rev-index --verify $packdir/pack-$pack.pack &&

	# Intentionally corrupt the reverse index.
	chmod u+w $rev &&
	printf "xxxx" | dd of=$rev bs=1 count=4 conv=notrunc &&

	test_must_fail but index-pack --rev-index --verify \
		$packdir/pack-$pack.pack 2>err &&
	grep "validation error" err
'

test_expect_success 'index-pack infers reverse index name with -o' '
	but index-pack --rev-index -o other.idx $packdir/pack-$pack.pack &&
	test_path_is_file other.idx &&
	test_path_is_file other.rev
'

test_expect_success 'pack-objects respects pack.writeReverseIndex' '
	test_when_finished "rm -fr pack-1-*" &&

	but -c pack.writeReverseIndex= pack-objects --all pack-1 &&
	test_path_is_missing pack-1-*.rev &&

	but -c pack.writeReverseIndex=false pack-objects --all pack-1 &&
	test_path_is_missing pack-1-*.rev &&

	but -c pack.writeReverseIndex=true pack-objects --all pack-1 &&
	test_path_is_file pack-1-*.rev
'

test_expect_success 'reverse index is not generated when available on disk' '
	test_index_pack true &&
	test_path_is_file $rev &&

	but rev-parse HEAD >tip &&
	BUT_TEST_REV_INDEX_DIE_IN_MEMORY=1 but cat-file \
		--batch-check="%(objectsize:disk)" <tip
'

test_expect_success 'revindex in-memory vs on-disk' '
	but init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_cummit cummit &&

		but rev-list --objects --no-object-names --all >objects &&

		but -c pack.writeReverseIndex=false repack -ad &&
		test_path_is_missing $packdir/pack-*.rev &&
		but cat-file --batch-check="%(objectsize:disk) %(objectname)" \
			<objects >in-core &&

		but -c pack.writeReverseIndex=true repack -ad &&
		test_path_is_file $packdir/pack-*.rev &&
		but cat-file --batch-check="%(objectsize:disk) %(objectname)" \
			<objects >on-disk &&

		test_cmp on-disk in-core
	)
'
test_done
