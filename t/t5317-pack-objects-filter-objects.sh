#!/bin/sh

test_description='but pack-objects using object filtering'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# Test blob:none filter.

test_expect_success 'setup r1' '
	echo "{print \$1}" >print_1.awk &&
	echo "{print \$2}" >print_2.awk &&

	but init r1 &&
	for n in 1 2 3 4 5
	do
		echo "This is file: $n" > r1/file.$n &&
		but -C r1 add file.$n &&
		but -C r1 cummit -m "$n" || return 1
	done
'

test_expect_success 'verify blob count in normal packfile' '
	but -C r1 ls-files -s file.1 file.2 file.3 file.4 file.5 \
		>ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	but -C r1 pack-objects --revs --stdout >all.pack <<-EOF &&
	HEAD
	EOF
	but -C r1 index-pack ../all.pack &&

	but -C r1 verify-pack -v ../all.pack >verify_result &&
	grep blob verify_result |
	awk -f print_1.awk |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify blob:none packfile has no blobs' '
	but -C r1 pack-objects --revs --stdout --filter=blob:none >filter.pack <<-EOF &&
	HEAD
	EOF
	but -C r1 index-pack ../filter.pack &&

	but -C r1 verify-pack -v ../filter.pack >verify_result &&
	! grep blob verify_result
'

test_expect_success 'verify normal and blob:none packfiles have same cummits/trees' '
	but -C r1 verify-pack -v ../all.pack >verify_result &&
	grep -E "cummit|tree" verify_result |
	awk -f print_1.awk |
	sort >expected &&

	but -C r1 verify-pack -v ../filter.pack >verify_result &&
	grep -E "cummit|tree" verify_result |
	awk -f print_1.awk |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'get an error for missing tree object' '
	but init r5 &&
	echo foo >r5/foo &&
	but -C r5 add foo &&
	but -C r5 cummit -m "foo" &&
	but -C r5 rev-parse HEAD^{tree} >tree &&
	del=$(sed "s|..|&/|" tree) &&
	rm r5/.but/objects/$del &&
	test_must_fail but -C r5 pack-objects --revs --stdout 2>bad_tree <<-EOF &&
	HEAD
	EOF
	grep "bad tree object" bad_tree
'

test_expect_success 'setup for tests of tree:0' '
	mkdir r1/subtree &&
	echo "This is a file in a subtree" >r1/subtree/file &&
	but -C r1 add subtree/file &&
	but -C r1 cummit -m subtree
'

test_expect_success 'verify tree:0 packfile has no blobs or trees' '
	but -C r1 pack-objects --revs --stdout --filter=tree:0 >cummitsonly.pack <<-EOF &&
	HEAD
	EOF
	but -C r1 index-pack ../cummitsonly.pack &&
	but -C r1 verify-pack -v ../cummitsonly.pack >objs &&
	! grep -E "tree|blob" objs
'

test_expect_success 'grab tree directly when using tree:0' '
	# We should get the tree specified directly but not its blobs or subtrees.
	but -C r1 pack-objects --revs --stdout --filter=tree:0 >cummitsonly.pack <<-EOF &&
	HEAD:
	EOF
	but -C r1 index-pack ../cummitsonly.pack &&
	but -C r1 verify-pack -v ../cummitsonly.pack >objs &&
	awk "/tree|blob/{print \$1}" objs >trees_and_blobs &&
	but -C r1 rev-parse HEAD: >expected &&
	test_cmp expected trees_and_blobs
'

# Test blob:limit=<n>[kmg] filter.
# We boundary test around the size parameter.  The filter is strictly less than
# the value, so size 500 and 1000 should have the same results, but 1001 should
# filter more.

test_expect_success 'setup r2' '
	but init r2 &&
	for n in 1000 10000
	do
		printf "%"$n"s" X > r2/large.$n &&
		but -C r2 add large.$n &&
		but -C r2 cummit -m "$n" || return 1
	done
'

test_expect_success 'verify blob count in normal packfile' '
	but -C r2 ls-files -s large.1000 large.10000 >ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	but -C r2 pack-objects --revs --stdout >all.pack <<-EOF &&
	HEAD
	EOF
	but -C r2 index-pack ../all.pack &&

	but -C r2 verify-pack -v ../all.pack >verify_result &&
	grep blob verify_result |
	awk -f print_1.awk |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify blob:limit=500 omits all blobs' '
	but -C r2 pack-objects --revs --stdout --filter=blob:limit=500 >filter.pack <<-EOF &&
	HEAD
	EOF
	but -C r2 index-pack ../filter.pack &&

	but -C r2 verify-pack -v ../filter.pack >verify_result &&
	! grep blob verify_result
'

test_expect_success 'verify blob:limit=1000' '
	but -C r2 pack-objects --revs --stdout --filter=blob:limit=1000 >filter.pack <<-EOF &&
	HEAD
	EOF
	but -C r2 index-pack ../filter.pack &&

	but -C r2 verify-pack -v ../filter.pack >verify_result &&
	! grep blob verify_result
'

test_expect_success 'verify blob:limit=1001' '
	but -C r2 ls-files -s large.1000 >ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	but -C r2 pack-objects --revs --stdout --filter=blob:limit=1001 >filter.pack <<-EOF &&
	HEAD
	EOF
	but -C r2 index-pack ../filter.pack &&

	but -C r2 verify-pack -v ../filter.pack >verify_result &&
	grep blob verify_result |
	awk -f print_1.awk |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify blob:limit=10001' '
	but -C r2 ls-files -s large.1000 large.10000 >ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	but -C r2 pack-objects --revs --stdout --filter=blob:limit=10001 >filter.pack <<-EOF &&
	HEAD
	EOF
	but -C r2 index-pack ../filter.pack &&

	but -C r2 verify-pack -v ../filter.pack >verify_result &&
	grep blob verify_result |
	awk -f print_1.awk |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify blob:limit=1k' '
	but -C r2 ls-files -s large.1000 >ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	but -C r2 pack-objects --revs --stdout --filter=blob:limit=1k >filter.pack <<-EOF &&
	HEAD
	EOF
	but -C r2 index-pack ../filter.pack &&

	but -C r2 verify-pack -v ../filter.pack >verify_result &&
	grep blob verify_result |
	awk -f print_1.awk |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify explicitly specifying oversized blob in input' '
	but -C r2 ls-files -s large.1000 large.10000 >ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	echo HEAD >objects &&
	but -C r2 rev-parse HEAD:large.10000 >>objects &&
	but -C r2 pack-objects --revs --stdout --filter=blob:limit=1k <objects >filter.pack &&
	but -C r2 index-pack ../filter.pack &&

	but -C r2 verify-pack -v ../filter.pack >verify_result &&
	grep blob verify_result |
	awk -f print_1.awk |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify blob:limit=1m' '
	but -C r2 ls-files -s large.1000 large.10000 >ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	but -C r2 pack-objects --revs --stdout --filter=blob:limit=1m >filter.pack <<-EOF &&
	HEAD
	EOF
	but -C r2 index-pack ../filter.pack &&

	but -C r2 verify-pack -v ../filter.pack >verify_result &&
	grep blob verify_result |
	awk -f print_1.awk |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify normal and blob:limit packfiles have same cummits/trees' '
	but -C r2 verify-pack -v ../all.pack >verify_result &&
	grep -E "cummit|tree" verify_result |
	awk -f print_1.awk |
	sort >expected &&

	but -C r2 verify-pack -v ../filter.pack >verify_result &&
	grep -E "cummit|tree" verify_result |
	awk -f print_1.awk |
	sort >observed &&

	test_cmp expected observed
'

# Test sparse:path=<path> filter.
# !!!!
# NOTE: sparse:path filter support has been dropped for security reasons,
# so the tests have been changed to make sure that using it fails.
# !!!!
# Use a local file containing a sparse-checkout specification to filter
# out blobs not required for the corresponding sparse-checkout.  We do not
# require sparse-checkout to actually be enabled.

test_expect_success 'setup r3' '
	but init r3 &&
	mkdir r3/dir1 &&
	for n in sparse1 sparse2
	do
		echo "This is file: $n" > r3/$n &&
		but -C r3 add $n &&
		echo "This is file: dir1/$n" > r3/dir1/$n &&
		but -C r3 add dir1/$n || return 1
	done &&
	but -C r3 cummit -m "sparse" &&
	echo dir1/ >pattern1 &&
	echo sparse1 >pattern2
'

test_expect_success 'verify blob count in normal packfile' '
	but -C r3 ls-files -s sparse1 sparse2 dir1/sparse1 dir1/sparse2 \
		>ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	but -C r3 pack-objects --revs --stdout >all.pack <<-EOF &&
	HEAD
	EOF
	but -C r3 index-pack ../all.pack &&

	but -C r3 verify-pack -v ../all.pack >verify_result &&
	grep blob verify_result |
	awk -f print_1.awk |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify sparse:path=pattern1 fails' '
	test_must_fail but -C r3 pack-objects --revs --stdout \
		--filter=sparse:path=../pattern1 <<-EOF
	HEAD
	EOF
'

test_expect_success 'verify sparse:path=pattern2 fails' '
	test_must_fail but -C r3 pack-objects --revs --stdout \
		--filter=sparse:path=../pattern2 <<-EOF
	HEAD
	EOF
'

# Test sparse:oid=<oid-ish> filter.
# Use a blob containing a sparse-checkout specification to filter
# out blobs not required for the corresponding sparse-checkout.  We do not
# require sparse-checkout to actually be enabled.

test_expect_success 'setup r4' '
	but init r4 &&
	mkdir r4/dir1 &&
	for n in sparse1 sparse2
	do
		echo "This is file: $n" > r4/$n &&
		but -C r4 add $n &&
		echo "This is file: dir1/$n" > r4/dir1/$n &&
		but -C r4 add dir1/$n || return 1
	done &&
	echo dir1/ >r4/pattern &&
	but -C r4 add pattern &&
	but -C r4 cummit -m "pattern"
'

test_expect_success 'verify blob count in normal packfile' '
	but -C r4 ls-files -s pattern sparse1 sparse2 dir1/sparse1 dir1/sparse2 \
		>ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	but -C r4 pack-objects --revs --stdout >all.pack <<-EOF &&
	HEAD
	EOF
	but -C r4 index-pack ../all.pack &&

	but -C r4 verify-pack -v ../all.pack >verify_result &&
	grep blob verify_result |
	awk -f print_1.awk |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify sparse:oid=OID' '
	but -C r4 ls-files -s dir1/sparse1 dir1/sparse2 >ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	but -C r4 ls-files -s pattern >staged &&
	oid=$(awk -f print_2.awk staged) &&
	but -C r4 pack-objects --revs --stdout --filter=sparse:oid=$oid >filter.pack <<-EOF &&
	HEAD
	EOF
	but -C r4 index-pack ../filter.pack &&

	but -C r4 verify-pack -v ../filter.pack >verify_result &&
	grep blob verify_result |
	awk -f print_1.awk |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify sparse:oid=oid-ish' '
	but -C r4 ls-files -s dir1/sparse1 dir1/sparse2 >ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	but -C r4 pack-objects --revs --stdout --filter=sparse:oid=main:pattern >filter.pack <<-EOF &&
	HEAD
	EOF
	but -C r4 index-pack ../filter.pack &&

	but -C r4 verify-pack -v ../filter.pack >verify_result &&
	grep blob verify_result |
	awk -f print_1.awk |
	sort >observed &&

	test_cmp expected observed
'

# Delete some loose objects and use pack-objects, but WITHOUT any filtering.
# This models previously omitted objects that we did not receive.

test_expect_success 'setup r1 - delete loose blobs' '
	but -C r1 ls-files -s file.1 file.2 file.3 file.4 file.5 \
		>ls_files_result &&
	awk -f print_2.awk ls_files_result |
	sort >expected &&

	for id in `cat expected | sed "s|..|&/|"`
	do
		rm r1/.but/objects/$id || return 1
	done
'

test_expect_success 'verify pack-objects fails w/ missing objects' '
	test_must_fail but -C r1 pack-objects --revs --stdout >miss.pack <<-EOF
	HEAD
	EOF
'

test_expect_success 'verify pack-objects fails w/ --missing=error' '
	test_must_fail but -C r1 pack-objects --revs --stdout --missing=error >miss.pack <<-EOF
	HEAD
	EOF
'

test_expect_success 'verify pack-objects w/ --missing=allow-any' '
	but -C r1 pack-objects --revs --stdout --missing=allow-any >miss.pack <<-EOF
	HEAD
	EOF
'

test_done
