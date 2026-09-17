#!/bin/sh

test_description='git repack --drop-filtered enumerates filtered promisor blobs'

. ./test-lib.sh

# Delete a loose or packed object from "repo".
delete_object () {
	local repo="$1" &&
	local obj="$2" &&
	local path="$repo/.git/objects/$(test_oid_to_path "$obj")" &&
	rm "$path"
}

# Pack the objects into a promisor pack inside "repo". It is a pack
# accompanied by an empty ".promisor" marker file. Objects
# in such a pack are treated as recoverable from the promisor remote.
pack_as_from_promisor () {
	HASH=$(git -C repo pack-objects .git/objects/pack/pack) &&
	>repo/.git/objects/pack/pack-$HASH.promisor &&
	echo $HASH
}

# Write a blob of $1 bytes into "repo", record it as coming from the
# promisor remote, and remove the loose copy so the object is only
# present in the promisor pack.
promisor_blob () {
	test-tool genrandom "$1" "$2" >blob_content &&
	OID=$(git -C repo hash-object -w --stdin <blob_content) &&
	printf "%s\n" "$OID" | pack_as_from_promisor >/dev/null &&
	delete_object repo "$OID" &&
	echo "$OID"
}

# Check option validation before any promisor walk
test_expect_success 'setup plain repo for validation' '
	git init plain &&
	test_commit -C plain initial &&
	git clone --bare plain plain.git &&
	git -C plain.git repack -a -d
'

test_expect_success '--drop-filtered requires --filter' '
	test_must_fail git -C plain.git repack --drop-filtered --dry-run -a 2>err &&
	test_grep "drop-filtered requires --filter" err
'

test_expect_success '--drop-filtered cannot be used with --filter-to' '
	test_must_fail git -C plain.git repack --drop-filtered \
		--filter=blob:limit=1k --filter-to=./filter-out 2>err &&
	test_grep "options .--drop-filtered. and .--filter-to. cannot be used together" err
'

test_expect_success '--dry-run only takes effect with --drop-filtered' '
	test_must_fail git -C plain.git repack --dry-run 2>err &&
	test_grep "dry-run only takes effect with --drop-filtered" err
'

test_expect_success '--drop-filtered requires -a' '
	test_must_fail git -C plain.git repack --drop-filtered \
		--filter=blob:limit=1k --dry-run 2>err &&
	test_grep "drop-filtered requires -a" err
'

test_expect_success '--drop-filtered fails with --write-bitmap-index' '
	test_must_fail git -C plain.git repack --drop-filtered \
		--filter=blob:limit=1k --dry-run -a -b 2>err &&
	test_grep "options .--drop-filtered. and .--write-bitmap-index. cannot be used together" err
'

test_expect_success '--drop-filtered rejects explicit -b even when repack.writeBitmaps=true' '
	test_must_fail git -C plain.git -c repack.writeBitmaps=true \
		repack --drop-filtered --filter=blob:limit=1k --dry-run -a -b 2>err &&
	test_grep "options .--drop-filtered. and .--write-bitmap-index. cannot be used together" err
'

test_expect_success '--drop-filtered fails without a promisor remote' '
	test_must_fail git -C plain.git repack --drop-filtered \
		--filter=blob:limit=1k --dry-run -a 2>err &&
	test_grep "drop-filtered requires a promisor remote" err
'

# Enumeration tests using promisor pack
test_expect_success 'setup repo with a promisor remote' '
	rm -rf repo &&
	test_create_repo repo &&
	test_commit -C repo base &&

	# Mark the repo as a partial clone with a promisor remote so the
	# promisor walk and the safety guard are satisfied.
	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone origin &&
	git -C repo config remote.origin.promisor true &&
	git -C repo config remote.origin.url "." &&

	BIG=$(promisor_blob big 3072) &&
	SMALL=$(promisor_blob small 512) &&
	echo "$BIG" >big_oid &&
	echo "$SMALL" >small_oid
'

test_expect_success 'promisor blob over the threshold is listed' '
	BIG=$(cat big_oid) &&
	SMALL=$(cat small_oid) &&

	git -C repo -c repack.writeBitmaps=false \
		repack --drop-filtered --filter=blob:limit=1k --dry-run -a >out &&

	test_grep "$BIG" out &&
	test_grep ! "$SMALL" out
'

test_expect_success 'locally created blob is never listed' '
	BIG=$(cat big_oid) &&

	# Large blob that exists only locally must never be a drop candidate.
	# Dropping it would be unrecoverable.
	test-tool genrandom local 4096 >local_content &&
	LOCAL=$(git -C repo hash-object -w --stdin <local_content) &&

	git -C repo -c repack.writeBitmaps=false \
		repack --drop-filtered --filter=blob:limit=1k --dry-run -a >out &&

	test_grep "$BIG" out &&
	test_grep ! "$LOCAL" out
'

test_expect_success '--dry-run does not remove the filtered objects' '
	BIG=$(cat big_oid) &&

	git -C repo -c repack.writeBitmaps=false \
		repack --drop-filtered --filter=blob:limit=1k --dry-run -a >out &&

	# Candidate blob must still be present after a dry run.
	git -C repo cat-file -e "$BIG"
'

test_expect_success '--drop-filtered removes the promisor blob locally' '
	BIG=$(cat big_oid) &&
	SMALL=$(cat small_oid) &&

	git -C repo -c repack.writeBitmaps=false \
		repack --drop-filtered --filter=blob:limit=1k -a &&

	git -C repo cat-file --batch-all-objects --batch-check="%(objectname)" >present &&
	test_grep ! "$BIG" present &&
	test_grep "$SMALL" present
'

test_expect_success '--drop-filtered refuses when a merge is in progress' '
	test_when_finished "git -C repo merge --abort || :" &&

	# Create a conflicting merge so wt_status reports it.
	git -C repo checkout -B mergebase base &&
	echo one >repo/conflict.txt &&
	git -C repo add conflict.txt &&
	git -C repo commit -m one &&

	git -C repo checkout -B mergeother base &&
	echo two >repo/conflict.txt &&
	git -C repo add conflict.txt &&
	git -C repo commit -m two &&

	test_must_fail git -C repo merge mergebase &&

	test_must_fail git -C repo -c repack.writeBitmaps=false \
		repack --drop-filtered --filter=blob:limit=1k --dry-run -a 2>err &&
	test_grep "in progress" err
'

test_expect_success '--drop-filtered refuses to drop an index-referenced blob' '
	# Create a large blob, add it to the index and make it a promisor object
	# so the index references it and enumeration picks it up.
	test-tool genrandom idx 4096 >repo/tracked-big.bin &&
	git -C repo add tracked-big.bin &&
	OID=$(git -C repo rev-parse :tracked-big.bin) &&
	printf "%s\n" "$OID" | pack_as_from_promisor >/dev/null &&
	delete_object repo "$OID" &&

	test_must_fail git -C repo -c repack.writeBitmaps=false \
		repack --drop-filtered --filter=blob:limit=1k --dry-run -a 2>err &&
	test_grep "referenced by the current index" err
'

test_done
