#!/bin/sh

test_description='git pack-objects using object filtering'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# Test blob:none filter.

test_expect_success 'setup r1' '
	git init r1 &&
	for n in 1 2 3 4 5
	do
		echo "This is file: $n" > r1/file.$n &&
		git -C r1 add file.$n &&
		git -C r1 commit -m "$n" || return 1
	done
'

parse_verify_pack_blob_oid () {
	awk '{print $1}' -
}

test_expect_success 'verify blob count in normal packfile' '
	git -C r1 ls-files -s file.1 file.2 file.3 file.4 file.5 \
		>ls_files_result &&
	test_parse_ls_files_stage_oids <ls_files_result |
	sort >expected &&

	git -C r1 pack-objects --revs --stdout >all.pack <<-EOF &&
	HEAD
	EOF
	git -C r1 index-pack ../all.pack &&

	git -C r1 verify-pack -v ../all.pack >verify_result &&
	grep blob verify_result |
	parse_verify_pack_blob_oid |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify blob:none packfile has no blobs' '
	git -C r1 pack-objects --revs --stdout --filter=blob:none >filter.pack <<-EOF &&
	HEAD
	EOF
	git -C r1 index-pack ../filter.pack &&

	git -C r1 verify-pack -v ../filter.pack >verify_result &&
	! grep blob verify_result
'

test_expect_success 'verify blob:none packfile without --stdout' '
	git -C r1 pack-objects --revs --filter=blob:none mypackname >packhash <<-EOF &&
	HEAD
	EOF
	git -C r1 verify-pack -v "mypackname-$(cat packhash).pack" >verify_result &&
	! grep blob verify_result
'

test_expect_success 'verify normal and blob:none packfiles have same commits/trees' '
	git -C r1 verify-pack -v ../all.pack >verify_result &&
	grep -E "commit|tree" verify_result |
	parse_verify_pack_blob_oid |
	sort >expected &&

	git -C r1 verify-pack -v ../filter.pack >verify_result &&
	grep -E "commit|tree" verify_result |
	parse_verify_pack_blob_oid |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'get an error for missing tree object' '
	git init r5 &&
	echo foo >r5/foo &&
	git -C r5 add foo &&
	git -C r5 commit -m "foo" &&
	git -C r5 rev-parse HEAD^{tree} >tree &&
	del=$(sed "s|..|&/|" tree) &&
	rm r5/.git/objects/$del &&
	test_must_fail git -C r5 pack-objects --revs --stdout 2>bad_tree <<-EOF &&
	HEAD
	EOF
	grep "bad tree object" bad_tree
'

test_expect_success 'setup for tests of tree:0' '
	mkdir r1/subtree &&
	echo "This is a file in a subtree" >r1/subtree/file &&
	git -C r1 add subtree/file &&
	git -C r1 commit -m subtree
'

test_expect_success 'verify tree:0 packfile has no blobs or trees' '
	git -C r1 pack-objects --revs --stdout --filter=tree:0 >commitsonly.pack <<-EOF &&
	HEAD
	EOF
	git -C r1 index-pack ../commitsonly.pack &&
	git -C r1 verify-pack -v ../commitsonly.pack >objs &&
	! grep -E "tree|blob" objs
'

test_expect_success 'grab tree directly when using tree:0' '
	# We should get the tree specified directly but not its blobs or subtrees.
	git -C r1 pack-objects --revs --stdout --filter=tree:0 >commitsonly.pack <<-EOF &&
	HEAD:
	EOF
	git -C r1 index-pack ../commitsonly.pack &&
	git -C r1 verify-pack -v ../commitsonly.pack >objs &&
	awk "/tree|blob/{print \$1}" objs >trees_and_blobs &&
	git -C r1 rev-parse HEAD: >expected &&
	test_cmp expected trees_and_blobs
'

# Test blob:limit=<n>[kmg] filter.
# We boundary test around the size parameter.  The filter is strictly less than
# the value, so size 500 and 1000 should have the same results, but 1001 should
# filter more.

test_expect_success 'setup r2' '
	git init r2 &&
	for n in 1000 10000
	do
		printf "%"$n"s" X > r2/large.$n &&
		git -C r2 add large.$n &&
		git -C r2 commit -m "$n" || return 1
	done
'

test_expect_success 'verify blob count in normal packfile' '
	git -C r2 ls-files -s large.1000 large.10000 >ls_files_result &&
	test_parse_ls_files_stage_oids <ls_files_result |
	sort >expected &&

	git -C r2 pack-objects --revs --stdout >all.pack <<-EOF &&
	HEAD
	EOF
	git -C r2 index-pack ../all.pack &&

	git -C r2 verify-pack -v ../all.pack >verify_result &&
	grep blob verify_result |
	parse_verify_pack_blob_oid |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify blob:limit=500 omits all blobs' '
	git -C r2 pack-objects --revs --stdout --filter=blob:limit=500 >filter.pack <<-EOF &&
	HEAD
	EOF
	git -C r2 index-pack ../filter.pack &&

	git -C r2 verify-pack -v ../filter.pack >verify_result &&
	! grep blob verify_result
'

test_expect_success 'verify blob:limit=1000' '
	git -C r2 pack-objects --revs --stdout --filter=blob:limit=1000 >filter.pack <<-EOF &&
	HEAD
	EOF
	git -C r2 index-pack ../filter.pack &&

	git -C r2 verify-pack -v ../filter.pack >verify_result &&
	! grep blob verify_result
'

test_expect_success 'verify blob:limit=1001' '
	git -C r2 ls-files -s large.1000 >ls_files_result &&
	test_parse_ls_files_stage_oids <ls_files_result |
	sort >expected &&

	git -C r2 pack-objects --revs --stdout --filter=blob:limit=1001 >filter.pack <<-EOF &&
	HEAD
	EOF
	git -C r2 index-pack ../filter.pack &&

	git -C r2 verify-pack -v ../filter.pack >verify_result &&
	grep blob verify_result |
	parse_verify_pack_blob_oid |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify blob:limit=10001' '
	git -C r2 ls-files -s large.1000 large.10000 >ls_files_result &&
	test_parse_ls_files_stage_oids <ls_files_result |
	sort >expected &&

	git -C r2 pack-objects --revs --stdout --filter=blob:limit=10001 >filter.pack <<-EOF &&
	HEAD
	EOF
	git -C r2 index-pack ../filter.pack &&

	git -C r2 verify-pack -v ../filter.pack >verify_result &&
	grep blob verify_result |
	parse_verify_pack_blob_oid |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify blob:limit=1k' '
	git -C r2 ls-files -s large.1000 >ls_files_result &&
	test_parse_ls_files_stage_oids <ls_files_result |
	sort >expected &&

	git -C r2 pack-objects --revs --stdout --filter=blob:limit=1k >filter.pack <<-EOF &&
	HEAD
	EOF
	git -C r2 index-pack ../filter.pack &&

	git -C r2 verify-pack -v ../filter.pack >verify_result &&
	grep blob verify_result |
	parse_verify_pack_blob_oid |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify explicitly specifying oversized blob in input' '
	git -C r2 ls-files -s large.1000 large.10000 >ls_files_result &&
	test_parse_ls_files_stage_oids <ls_files_result |
	sort >expected &&

	echo HEAD >objects &&
	git -C r2 rev-parse HEAD:large.10000 >>objects &&
	git -C r2 pack-objects --revs --stdout --filter=blob:limit=1k <objects >filter.pack &&
	git -C r2 index-pack ../filter.pack &&

	git -C r2 verify-pack -v ../filter.pack >verify_result &&
	grep blob verify_result |
	parse_verify_pack_blob_oid |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify blob:limit=1m' '
	git -C r2 ls-files -s large.1000 large.10000 >ls_files_result &&
	test_parse_ls_files_stage_oids <ls_files_result |
	sort >expected &&

	git -C r2 pack-objects --revs --stdout --filter=blob:limit=1m >filter.pack <<-EOF &&
	HEAD
	EOF
	git -C r2 index-pack ../filter.pack &&

	git -C r2 verify-pack -v ../filter.pack >verify_result &&
	grep blob verify_result |
	parse_verify_pack_blob_oid |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify normal and blob:limit packfiles have same commits/trees' '
	git -C r2 verify-pack -v ../all.pack >verify_result &&
	grep -E "commit|tree" verify_result |
	parse_verify_pack_blob_oid |
	sort >expected &&

	git -C r2 verify-pack -v ../filter.pack >verify_result &&
	grep -E "commit|tree" verify_result |
	parse_verify_pack_blob_oid |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify small limit and big limit results in small limit' '
	git -C r2 ls-files -s large.1000 >ls_files_result &&
	test_parse_ls_files_stage_oids <ls_files_result |
	sort >expected &&

	git -C r2 pack-objects --revs --stdout --filter=blob:limit=1001 \
		--filter=blob:limit=10001 >filter.pack <<-EOF &&
	HEAD
	EOF
	git -C r2 index-pack ../filter.pack &&

	git -C r2 verify-pack -v ../filter.pack >verify_result &&
	grep blob verify_result |
	parse_verify_pack_blob_oid |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify big limit and small limit results in small limit' '
	git -C r2 ls-files -s large.1000 >ls_files_result &&
	test_parse_ls_files_stage_oids <ls_files_result |
	sort >expected &&

	git -C r2 pack-objects --revs --stdout --filter=blob:limit=10001 \
		--filter=blob:limit=1001 >filter.pack <<-EOF &&
	HEAD
	EOF
	git -C r2 index-pack ../filter.pack &&

	git -C r2 verify-pack -v ../filter.pack >verify_result &&
	grep blob verify_result |
	parse_verify_pack_blob_oid |
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
	git init r3 &&
	mkdir r3/dir1 &&
	for n in sparse1 sparse2
	do
		echo "This is file: $n" > r3/$n &&
		git -C r3 add $n &&
		echo "This is file: dir1/$n" > r3/dir1/$n &&
		git -C r3 add dir1/$n || return 1
	done &&
	git -C r3 commit -m "sparse" &&
	echo dir1/ >pattern1 &&
	echo sparse1 >pattern2
'

test_expect_success 'verify blob count in normal packfile' '
	git -C r3 ls-files -s sparse1 sparse2 dir1/sparse1 dir1/sparse2 \
		>ls_files_result &&
	test_parse_ls_files_stage_oids <ls_files_result |
	sort >expected &&

	git -C r3 pack-objects --revs --stdout >all.pack <<-EOF &&
	HEAD
	EOF
	git -C r3 index-pack ../all.pack &&

	git -C r3 verify-pack -v ../all.pack >verify_result &&
	grep blob verify_result |
	parse_verify_pack_blob_oid |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify sparse:path=pattern1 fails' '
	test_must_fail git -C r3 pack-objects --revs --stdout \
		--filter=sparse:path=../pattern1 <<-EOF
	HEAD
	EOF
'

test_expect_success 'verify sparse:path=pattern2 fails' '
	test_must_fail git -C r3 pack-objects --revs --stdout \
		--filter=sparse:path=../pattern2 <<-EOF
	HEAD
	EOF
'

# Test sparse:oid=<oid-ish> filter.
# Use a blob containing a sparse-checkout specification to filter
# out blobs not required for the corresponding sparse-checkout.  We do not
# require sparse-checkout to actually be enabled.

test_expect_success 'setup r4' '
	git init r4 &&
	mkdir r4/dir1 &&
	for n in sparse1 sparse2
	do
		echo "This is file: $n" > r4/$n &&
		git -C r4 add $n &&
		echo "This is file: dir1/$n" > r4/dir1/$n &&
		git -C r4 add dir1/$n || return 1
	done &&
	echo dir1/ >r4/pattern &&
	git -C r4 add pattern &&
	git -C r4 commit -m "pattern"
'

test_expect_success 'verify blob count in normal packfile' '
	git -C r4 ls-files -s pattern sparse1 sparse2 dir1/sparse1 dir1/sparse2 \
		>ls_files_result &&
	test_parse_ls_files_stage_oids <ls_files_result |
	sort >expected &&

	git -C r4 pack-objects --revs --stdout >all.pack <<-EOF &&
	HEAD
	EOF
	git -C r4 index-pack ../all.pack &&

	git -C r4 verify-pack -v ../all.pack >verify_result &&
	grep blob verify_result |
	parse_verify_pack_blob_oid |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify sparse:oid=OID' '
	git -C r4 ls-files -s dir1/sparse1 dir1/sparse2 >ls_files_result &&
	test_parse_ls_files_stage_oids <ls_files_result |
	sort >expected &&

	git -C r4 ls-files -s pattern >staged &&
	oid=$(test_parse_ls_files_stage_oids <staged) &&
	git -C r4 pack-objects --revs --stdout --filter=sparse:oid=$oid >filter.pack <<-EOF &&
	HEAD
	EOF
	git -C r4 index-pack ../filter.pack &&

	git -C r4 verify-pack -v ../filter.pack >verify_result &&
	grep blob verify_result |
	parse_verify_pack_blob_oid |
	sort >observed &&

	test_cmp expected observed
'

test_expect_success 'verify sparse:oid=oid-ish' '
	git -C r4 ls-files -s dir1/sparse1 dir1/sparse2 >ls_files_result &&
	test_parse_ls_files_stage_oids <ls_files_result |
	sort >expected &&

	git -C r4 pack-objects --revs --stdout --filter=sparse:oid=main:pattern >filter.pack <<-EOF &&
	HEAD
	EOF
	git -C r4 index-pack ../filter.pack &&

	git -C r4 verify-pack -v ../filter.pack >verify_result &&
	grep blob verify_result |
	parse_verify_pack_blob_oid |
	sort >observed &&

	test_cmp expected observed
'

# Delete some loose objects and use pack-objects, but WITHOUT any filtering.
# This models previously omitted objects that we did not receive.

test_expect_success 'setup r1 - delete loose blobs' '
	git -C r1 ls-files -s file.1 file.2 file.3 file.4 file.5 \
		>ls_files_result &&
	test_parse_ls_files_stage_oids <ls_files_result |
	sort >expected &&

	for id in `sed "s|..|&/|" expected`
	do
		rm r1/.git/objects/$id || return 1
	done
'

test_expect_success 'verify pack-objects fails w/ missing objects' '
	test_must_fail git -C r1 pack-objects --revs --stdout >miss.pack <<-EOF
	HEAD
	EOF
'

test_expect_success 'verify pack-objects fails w/ --missing=error' '
	test_must_fail git -C r1 pack-objects --revs --stdout --missing=error >miss.pack <<-EOF
	HEAD
	EOF
'

test_expect_success 'verify pack-objects w/ --missing=allow-any' '
	git -C r1 pack-objects --revs --stdout --missing=allow-any >miss.pack <<-EOF
	HEAD
	EOF
'

# Test that --path-walk produces the same object set as standard traversal
# when using sparse:oid filters with cone-mode patterns.
#
# The sparse:oid filter restricts only blobs, not trees. Both standard
# and path-walk should produce identical sets of blobs, commits, and trees.

test_expect_success 'setup pw_sparse for path-walk comparison' '
	git init pw_sparse &&
	mkdir -p pw_sparse/inc/sub pw_sparse/exc/sub &&

	for n in 1 2
	do
		echo "inc $n" >pw_sparse/inc/file$n &&
		echo "inc sub $n" >pw_sparse/inc/sub/file$n &&
		echo "exc $n" >pw_sparse/exc/file$n &&
		echo "exc sub $n" >pw_sparse/exc/sub/file$n &&
		echo "root $n" >pw_sparse/root$n || return 1
	done &&

	git -C pw_sparse add . &&
	git -C pw_sparse commit -m "first" &&

	echo "inc 1 modified" >pw_sparse/inc/file1 &&
	echo "exc 1 modified" >pw_sparse/exc/file1 &&
	echo "root 1 modified" >pw_sparse/root1 &&
	git -C pw_sparse add . &&
	git -C pw_sparse commit -m "second" &&

	# Cone-mode sparse pattern: include root + inc/
	printf "/*\n!/*/\n/inc/\n" |
	git -C pw_sparse hash-object -w --stdin >sparse_oid
'

test_expect_success 'sparse:oid with --path-walk produces same blobs' '
	oid=$(cat sparse_oid) &&

	git -C pw_sparse pack-objects --revs --stdout \
		--filter=sparse:oid=$oid >standard.pack <<-EOF &&
	HEAD
	EOF
	git -C pw_sparse index-pack ../standard.pack &&
	git -C pw_sparse verify-pack -v ../standard.pack >standard_verify &&

	git -C pw_sparse pack-objects --revs --stdout \
		--path-walk --filter=sparse:oid=$oid >pathwalk.pack <<-EOF &&
	HEAD
	EOF
	git -C pw_sparse index-pack ../pathwalk.pack &&
	git -C pw_sparse verify-pack -v ../pathwalk.pack >pathwalk_verify &&

	# Blobs must match exactly
	grep -E "^[0-9a-f]{40} blob" standard_verify |
	awk "{print \$1}" | sort >standard_blobs &&
	grep -E "^[0-9a-f]{40} blob" pathwalk_verify |
	awk "{print \$1}" | sort >pathwalk_blobs &&
	test_cmp standard_blobs pathwalk_blobs &&

	# Commits must match exactly
	grep -E "^[0-9a-f]{40} commit" standard_verify |
	awk "{print \$1}" | sort >standard_commits &&
	grep -E "^[0-9a-f]{40} commit" pathwalk_verify |
	awk "{print \$1}" | sort >pathwalk_commits &&
	test_cmp standard_commits pathwalk_commits
'

test_expect_success 'sparse:oid with --path-walk includes all trees' '
	# The sparse:oid filter restricts only blobs, not trees.
	# Both standard and path-walk should include the same trees.
	grep -E "^[0-9a-f]{40} tree" standard_verify |
	awk "{print \$1}" | sort >standard_trees &&
	grep -E "^[0-9a-f]{40} tree" pathwalk_verify |
	awk "{print \$1}" | sort >pathwalk_trees &&

	test_cmp standard_trees pathwalk_trees
'

# Test the edge case where the same tree/blob OID appears at both an
# in-cone and out-of-cone path. When sibling directories have identical
# contents, they share a tree OID. The path-walk defers marking objects
# SEEN until after checking sparse patterns, so an object at an out-of-cone
# path can still be discovered at an in-cone path.

test_expect_success 'setup pw_shared for shared OID across cone boundary' '
	git init pw_shared &&
	mkdir pw_shared/aaa pw_shared/zzz &&
	echo "shared content" >pw_shared/aaa/file &&
	echo "shared content" >pw_shared/zzz/file &&
	echo "root file" >pw_shared/rootfile &&
	git -C pw_shared add . &&
	git -C pw_shared commit -m "aaa and zzz share tree OID" &&

	# Verify they share a tree OID
	aaa_tree=$(git -C pw_shared rev-parse HEAD:aaa) &&
	zzz_tree=$(git -C pw_shared rev-parse HEAD:zzz) &&
	test "$aaa_tree" = "$zzz_tree" &&

	# Cone pattern: include root + zzz/ (not aaa/)
	printf "/*\n!/*/\n/zzz/\n" |
	git -C pw_shared hash-object -w --stdin >shared_sparse_oid
'

test_expect_success 'shared tree OID: --path-walk blobs match standard' '
	oid=$(cat shared_sparse_oid) &&

	git -C pw_shared pack-objects --revs --stdout \
		--filter=sparse:oid=$oid >shared_std.pack <<-EOF &&
	HEAD
	EOF
	git -C pw_shared index-pack ../shared_std.pack &&
	git -C pw_shared verify-pack -v ../shared_std.pack >shared_std_verify &&

	git -C pw_shared pack-objects --revs --stdout \
		--path-walk --filter=sparse:oid=$oid >shared_pw.pack <<-EOF &&
	HEAD
	EOF
	git -C pw_shared index-pack ../shared_pw.pack &&
	git -C pw_shared verify-pack -v ../shared_pw.pack >shared_pw_verify &&

	grep -E "^[0-9a-f]{40} blob" shared_std_verify |
	awk "{print \$1}" | sort >shared_std_blobs &&
	grep -E "^[0-9a-f]{40} blob" shared_pw_verify |
	awk "{print \$1}" | sort >shared_pw_blobs &&
	test_cmp shared_std_blobs shared_pw_blobs
'

test_done
