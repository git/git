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

test_expect_success 'specify blob explicitly prevents filtering' '
	file_3=$(git -C r1 ls-files -s file.3 |
		 awk -f print_2.awk) &&

	file_4=$(git -C r1 ls-files -s file.4 |
		 awk -f print_2.awk) &&

	git -C r1 rev-list --objects --filter=blob:none HEAD $file_3 >observed &&
	grep "$file_3" observed &&
	! grep "$file_4" observed
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
		echo "This is file: $n" > r3/$n
		git -C r3 add $n
		echo "This is file: dir1/$n" > r3/dir1/$n
		git -C r3 add dir1/$n
	done &&
	git -C r3 commit -m "sparse" &&
	echo dir1/ >pattern1 &&
	echo sparse1 >pattern2
'

test_expect_success 'verify sparse:path=pattern1 fails' '
	test_must_fail git -C r3 rev-list --quiet --objects \
		--filter-print-omitted --filter=sparse:path=../pattern1 HEAD
'

test_expect_success 'verify sparse:path=pattern2 fails' '
	test_must_fail git -C r3 rev-list --quiet --objects \
		--filter-print-omitted --filter=sparse:path=../pattern2 HEAD
'

# Test sparse:oid=<oid-ish> filter.
# Use a blob containing a sparse-checkout specification to filter
# out blobs not required for the corresponding sparse-checkout.  We do not
# require sparse-checkout to actually be enabled.

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

test_expect_success 'rev-list W/ --missing=print and --missing=allow-any for trees' '
	TREE=$(git -C r3 rev-parse HEAD:dir1) &&

	# Create a spare repo because we will be deleting objects from this one.
	git clone r3 r3.b &&

	rm r3.b/.git/objects/$(echo $TREE | sed "s|^..|&/|") &&

	git -C r3.b rev-list --quiet --missing=print --objects HEAD \
		>missing_objs 2>rev_list_err &&
	echo "?$TREE" >expected &&
	test_cmp expected missing_objs &&

	# do not complain when a missing tree cannot be parsed
	test_must_be_empty rev_list_err &&

	git -C r3.b rev-list --missing=allow-any --objects HEAD \
		>objs 2>rev_list_err &&
	! grep $TREE objs &&
	test_must_be_empty rev_list_err
'

# Test tree:0 filter.

test_expect_success 'verify tree:0 includes trees in "filtered" output' '
	git -C r3 rev-list --quiet --objects --filter-print-omitted \
		--filter=tree:0 HEAD >revs &&

	awk -f print_1.awk revs |
	sed s/~// |
	xargs -n1 git -C r3 cat-file -t >unsorted_filtered_types &&

	sort -u unsorted_filtered_types >filtered_types &&
	test_write_lines blob tree >expected &&
	test_cmp expected filtered_types
'

# Make sure tree:0 does not iterate through any trees.

test_expect_success 'verify skipping tree iteration when not collecting omits' '
	GIT_TRACE=1 git -C r3 rev-list \
		--objects --filter=tree:0 HEAD 2>filter_trace &&
	grep "Skipping contents of tree [.][.][.]" filter_trace >actual &&
	# One line for each commit traversed.
	test_line_count = 2 actual &&

	# Make sure no other trees were considered besides the root.
	! grep "Skipping contents of tree [^.]" filter_trace
'

# Test tree:# filters.

expect_has () {
	commit=$1 &&
	name=$2 &&

	hash=$(git -C r3 rev-parse $commit:$name) &&
	grep "^$hash $name$" actual
}

test_expect_success 'verify tree:1 includes root trees' '
	git -C r3 rev-list --objects --filter=tree:1 HEAD >actual &&

	# We should get two root directories and two commits.
	expect_has HEAD "" &&
	expect_has HEAD~1 ""  &&
	test_line_count = 4 actual
'

test_expect_success 'verify tree:2 includes root trees and immediate children' '
	git -C r3 rev-list --objects --filter=tree:2 HEAD >actual &&

	expect_has HEAD "" &&
	expect_has HEAD~1 "" &&
	expect_has HEAD dir1 &&
	expect_has HEAD pattern &&
	expect_has HEAD sparse1 &&
	expect_has HEAD sparse2 &&

	# There are also 2 commit objects
	test_line_count = 8 actual
'

test_expect_success 'verify tree:3 includes everything expected' '
	git -C r3 rev-list --objects --filter=tree:3 HEAD >actual &&

	expect_has HEAD "" &&
	expect_has HEAD~1 "" &&
	expect_has HEAD dir1 &&
	expect_has HEAD dir1/sparse1 &&
	expect_has HEAD dir1/sparse2 &&
	expect_has HEAD pattern &&
	expect_has HEAD sparse1 &&
	expect_has HEAD sparse2 &&

	# There are also 2 commit objects
	test_line_count = 10 actual
'

# Test provisional omit collection logic with a repo that has objects appearing
# at multiple depths - first deeper than the filter's threshold, then shallow.

test_expect_success 'setup r4' '
	git init r4 &&

	echo foo > r4/foo &&
	mkdir r4/subdir &&
	echo bar > r4/subdir/bar &&

	mkdir r4/filt &&
	cp -r r4/foo r4/subdir r4/filt &&

	git -C r4 add foo subdir filt &&
	git -C r4 commit -m "commit msg"
'

expect_has_with_different_name () {
	repo=$1 &&
	name=$2 &&

	hash=$(git -C $repo rev-parse HEAD:$name) &&
	! grep "^$hash $name$" actual &&
	grep "^$hash " actual &&
	! grep "~$hash" actual
}

test_expect_success 'test tree:# filter provisional omit for blob and tree' '
	git -C r4 rev-list --objects --filter-print-omitted --filter=tree:2 \
		HEAD >actual &&
	expect_has_with_different_name r4 filt/foo &&
	expect_has_with_different_name r4 filt/subdir
'

test_expect_success 'verify skipping tree iteration when collecting omits' '
	GIT_TRACE=1 git -C r4 rev-list --filter-print-omitted \
		--objects --filter=tree:0 HEAD 2>filter_trace &&
	grep "^Skipping contents of tree " filter_trace >actual &&

	echo "Skipping contents of tree subdir/..." >expect &&
	test_cmp expect actual
'

# Test tree:<depth> where a tree is iterated to twice - once where a subentry is
# too deep to be included, and again where the blob inside it is shallow enough
# to be included. This makes sure we don't use LOFR_MARK_SEEN incorrectly (we
# can't use it because a tree can be iterated over again at a lower depth).

test_expect_success 'tree:<depth> where we iterate over tree at two levels' '
	git init r5 &&

	mkdir -p r5/a/subdir/b &&
	echo foo > r5/a/subdir/b/foo &&

	mkdir -p r5/subdir/b &&
	echo foo > r5/subdir/b/foo &&

	git -C r5 add a subdir &&
	git -C r5 commit -m "commit msg" &&

	git -C r5 rev-list --objects --filter=tree:4 HEAD >actual &&
	expect_has_with_different_name r5 a/subdir/b/foo
'

test_expect_success 'tree:<depth> which filters out blob but given as arg' '
	blob_hash=$(git -C r4 rev-parse HEAD:subdir/bar) &&

	git -C r4 rev-list --objects --filter=tree:1 HEAD $blob_hash >actual &&
	grep ^$blob_hash actual
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

# Test expansion of filter specs.

test_expect_success 'expand blob limit in protocol' '
	git -C r2 config --local uploadpack.allowfilter 1 &&
	GIT_TRACE_PACKET="$(pwd)/trace" git -c protocol.version=2 clone \
		--filter=blob:limit=1k "file://$(pwd)/r2" limit &&
	! grep "blob:limit=1k" trace &&
	grep "blob:limit=1024" trace
'

test_expect_success 'expand tree depth limit in protocol' '
	GIT_TRACE_PACKET="$(pwd)/tree_trace" git -c protocol.version=2 clone \
		--filter=tree:0k "file://$(pwd)/r2" tree &&
	! grep "tree:0k" tree_trace &&
	grep "tree:0" tree_trace
'

test_done
