#!/bin/sh

test_description='git repack --drop-filtered option validation'

. ./test-lib.sh

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

test_done
