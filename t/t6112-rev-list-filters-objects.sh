#!/bin/sh

test_description='git rev-list using object filtering'

. ./test-lib.sh

# Test the blob:none filter.

test_expect_success 'setup r1' '
	echo "{print \$1}" >print_1.awk &&
	echo "{print \$2}" >print_2.awk &&

	git init r1 &&
	for n in 1 2 3 4 5
	do
		echo "This is file: $n" > r1/file.$n
		git -C r1 add file.$n
		git -C r1 commit -m "$n"
	done
'

test_expect_success 'verify blob:none omits all 5 blobs' '
	git -C r1 ls-files -s file.1 file.2 file.3 file.4 file.5 \
		>ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	git -C r1 rev-list --quiet --objects --filter-print-omitted \
		--filter=blob:none HEAD >revs &&
	awk -f print_1.awk revs |
	sed "s/~//" |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify emitted+omitted == all' '
	git -C r1 rev-list --objects HEAD >revs &&
	awk -f print_1.awk revs |
	sort >expected &&

	git -C r1 rev-list --objects --filter-print-omitted --filter=blob:none \
		HEAD >revs &&
	awk -f print_1.awk revs |
	sed "s/~//" |
	sort >observed &&

	test_cmp expected observed
'


# Test blob:limit=<n>[kmg] filter.
# We boundary test around the size parameter.  The filter is strictly less than
# the value, so size 500 and 1000 should have the same results, but 1001 should
# filter more.

test_expect_success 'setup r2' '
	git init r2 &&
	for n in 1000 10000
	do
		printf "%"$n"s" X > r2/large.$n
		git -C r2 add large.$n
		git -C r2 commit -m "$n"
	done
'

test_expect_success 'verify blob:limit=500 omits all blobs' '
	git -C r2 ls-files -s large.1000 large.10000 >ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	git -C r2 rev-list --quiet --objects --filter-print-omitted \
		--filter=blob:limit=500 HEAD >revs &&
	awk -f print_1.awk revs |
	sed "s/~//" |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify emitted+omitted == all' '
	git -C r2 rev-list --objects HEAD >revs &&
	awk -f print_1.awk revs |
	sort >expected &&

	git -C r2 rev-list --objects --filter-print-omitted \
		--filter=blob:limit=500 HEAD >revs &&
	awk -f print_1.awk revs |
	sed "s/~//" |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify blob:limit=1000' '
	git -C r2 ls-files -s large.1000 large.10000 >ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	git -C r2 rev-list --quiet --objects --filter-print-omitted \
		--filter=blob:limit=1000 HEAD >revs &&
	awk -f print_1.awk revs |
	sed "s/~//" |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify blob:limit=1001' '
	git -C r2 ls-files -s large.10000 >ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	git -C r2 rev-list --quiet --objects --filter-print-omitted \
		--filter=blob:limit=1001 HEAD >revs &&
	awk -f print_1.awk revs |
	sed "s/~//" |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify blob:limit=1k' '
	git -C r2 ls-files -s large.10000 >ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	git -C r2 rev-list --quiet --objects --filter-print-omitted \
		--filter=blob:limit=1k HEAD >revs &&
	awk -f print_1.awk revs |
	sed "s/~//" |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify blob:limit=1m' '
	git -C r2 rev-list --quiet --objects --filter-print-omitted \
		--filter=blob:limit=1m HEAD >revs &&
	awk -f print_1.awk revs |
	sed "s/~//" |
	sort >observed &&

	test_must_be_empty observed
'

# Test sparse:path=<path> filter.
# Use a local file containing a sparse-checkout specification to filter
# out blobs not required for the corresponding sparse-checkout.  We do not
# require sparse-checkout to actually be enabled.

test_expect_success 'setup r3' '
	git init r3 &&
	mkdir r3/dir1 &&
	for n in sparse1 sparse2
	do
		echo "This is file: $n" > r3/$n
		git -C r3 add $n
		echo "This is file: dir1/$n" > r3/dir1/$n
		git -C r3 add dir1/$n
	done &&
	git -C r3 commit -m "sparse" &&
	echo dir1/ >pattern1 &&
	echo sparse1 >pattern2
'

test_expect_success 'verify sparse:path=pattern1 omits top-level files' '
	git -C r3 ls-files -s sparse1 sparse2 >ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	git -C r3 rev-list --quiet --objects --filter-print-omitted \
		--filter=sparse:path=../pattern1 HEAD >revs &&
	awk -f print_1.awk revs |
	sed "s/~//" |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify sparse:path=pattern2 omits both sparse2 files' '
	git -C r3 ls-files -s sparse2 dir1/sparse2 >ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	git -C r3 rev-list --quiet --objects --filter-print-omitted \
		--filter=sparse:path=../pattern2 HEAD >revs &&
	awk -f print_1.awk revs |
	sed "s/~//" |
	sort >observed &&

	test_cmp expected observed
'

# Test sparse:oid=<oid-ish> filter.
# Like sparse:path, but we get the sparse-checkout specification from
# a blob rather than a file on disk.

test_expect_success 'setup r3 part 2' '
	echo dir1/ >r3/pattern &&
	git -C r3 add pattern &&
	git -C r3 commit -m "pattern"
'

test_expect_success 'verify sparse:oid=OID omits top-level files' '
	git -C r3 ls-files -s pattern sparse1 sparse2 >ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	oid=$(git -C r3 ls-files -s pattern | awk -f print_2.awk) &&

	git -C r3 rev-list --quiet --objects --filter-print-omitted \
		--filter=sparse:oid=$oid HEAD >revs &&
	awk -f print_1.awk revs |
	sed "s/~//" |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify sparse:oid=oid-ish omits top-level files' '
	git -C r3 ls-files -s pattern sparse1 sparse2 >ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	git -C r3 rev-list --quiet --objects --filter-print-omitted \
		--filter=sparse:oid=master:pattern HEAD >revs &&
	awk -f print_1.awk revs |
	sed "s/~//" |
	sort >observed &&

	test_cmp expected observed
'

# Delete some loose objects and use rev-list, but WITHOUT any filtering.
# This models previously omitted objects that we did not receive.

test_expect_success 'rev-list W/ --missing=print' '
	git -C r1 ls-files -s file.1 file.2 file.3 file.4 file.5 \
		>ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	for id in `cat expected | sed "s|..|&/|"`
	do
		rm r1/.git/objects/$id
	done &&

	git -C r1 rev-list --quiet --missing=print --objects HEAD >revs &&
	awk -f print_1.awk revs |
	sed "s/?//" |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'rev-list W/O --missing fails' '
	test_must_fail git -C r1 rev-list --quiet --objects HEAD
'

test_expect_success 'rev-list W/ missing=allow-any' '
	git -C r1 rev-list --quiet --missing=allow-any --objects HEAD
'

test_done
