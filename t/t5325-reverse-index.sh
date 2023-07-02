#!/bin/sh

test_description='on-disk reverse index'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# The below tests want control over the 'pack.writeReverseIndex' setting
# themselves to assert various combinations of it with other options.
sane_unset GIT_TEST_NO_WRITE_REV_INDEX

packdir=.git/objects/pack

test_expect_success 'setup' '
	test_commit base &&

	test_config pack.writeReverseIndex false &&
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
		test_path_is_missing $rev || return 1
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

test_expect_success 'pack-objects respects pack.writeReverseIndex' '
	test_when_finished "rm -fr pack-1-*" &&

	git -c pack.writeReverseIndex= pack-objects --all pack-1 &&
	test_path_is_missing pack-1-*.rev &&

	git -c pack.writeReverseIndex=false pack-objects --all pack-1 &&
	test_path_is_missing pack-1-*.rev &&

	git -c pack.writeReverseIndex=true pack-objects --all pack-1 &&
	test_path_is_file pack-1-*.rev
'

test_expect_success 'reverse index is not generated when available on disk' '
	test_index_pack true &&
	test_path_is_file $rev &&

	git rev-parse HEAD >tip &&
	GIT_TEST_REV_INDEX_DIE_IN_MEMORY=1 git cat-file \
		--batch-check="%(objectsize:disk)" <tip
'

test_expect_success 'reverse index is ignored when pack.readReverseIndex is false' '
	test_index_pack true &&
	test_path_is_file $rev &&

	test_config pack.readReverseIndex false &&

	git rev-parse HEAD >tip &&
	GIT_TEST_REV_INDEX_DIE_ON_DISK=1 git cat-file \
		--batch-check="%(objectsize:disk)" <tip
'

test_expect_success 'revindex in-memory vs on-disk' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit commit &&

		git rev-list --objects --no-object-names --all >objects &&

		git -c pack.writeReverseIndex=false repack -ad &&
		test_path_is_missing $packdir/pack-*.rev &&
		git cat-file --batch-check="%(objectsize:disk) %(objectname)" \
			<objects >in-core &&

		git -c pack.writeReverseIndex=true repack -ad &&
		test_path_is_file $packdir/pack-*.rev &&
		git cat-file --batch-check="%(objectsize:disk) %(objectname)" \
			<objects >on-disk &&

		test_cmp on-disk in-core
	)
'

test_expect_success 'fsck succeeds on good rev-index' '
	test_when_finished rm -fr repo &&
	git init repo &&
	(
		cd repo &&

		test_commit commit &&
		git -c pack.writeReverseIndex=true repack -ad &&
		git fsck 2>err &&
		test_must_be_empty err
	)
'

test_expect_success 'set up rev-index corruption tests' '
	git init corrupt &&
	(
		cd corrupt &&

		test_commit commit &&
		git -c pack.writeReverseIndex=true repack -ad &&

		revfile=$(ls .git/objects/pack/pack-*.rev) &&
		chmod a+w $revfile &&
		cp $revfile $revfile.bak
	)
'

corrupt_rev_and_verify () {
	(
		pos="$1" &&
		value="$2" &&
		error="$3" &&

		cd corrupt &&
		revfile=$(ls .git/objects/pack/pack-*.rev) &&

		# Reset to original rev-file.
		cp $revfile.bak $revfile &&

		printf "$value" | dd of=$revfile bs=1 seek="$pos" conv=notrunc &&
		test_must_fail git fsck 2>err &&
		grep "$error" err
	)
}

test_expect_success 'fsck catches invalid checksum' '
	revfile=$(ls corrupt/.git/objects/pack/pack-*.rev) &&
	orig_size=$(wc -c <$revfile) &&
	hashpos=$((orig_size - 10)) &&
	corrupt_rev_and_verify $hashpos bogus \
		"invalid checksum"
'

test_expect_success 'fsck catches invalid row position' '
	corrupt_rev_and_verify 14 "\07" \
		"invalid rev-index position"
'

test_expect_success 'fsck catches invalid header: magic number' '
	corrupt_rev_and_verify 1 "\07" \
		"reverse-index file .* has unknown signature"
'

test_expect_success 'fsck catches invalid header: version' '
	corrupt_rev_and_verify 7 "\02" \
		"reverse-index file .* has unsupported version"
'

test_expect_success 'fsck catches invalid header: hash function' '
	corrupt_rev_and_verify 11 "\03" \
		"reverse-index file .* has unsupported hash id"
'

test_done
