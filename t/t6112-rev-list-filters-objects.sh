#!/bin/sh

test_description='git rev-list using object filtering'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# Test the blob:none filter.

test_expect_success 'setup r1' '
	echo "{print \$1}" >print_1.awk &&
	echo "{print \$2}" >print_2.awk &&

	git init r1 &&
	for n in 1 2 3 4 5
	do
		echo "This is file: $n" > r1/file.$n &&
		git -C r1 add file.$n &&
		git -C r1 commit -m "$n" || return 1
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
		printf "%"$n"s" X > r2/large.$n &&
		git -C r2 add large.$n &&
		git -C r2 commit -m "$n" || return 1
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

# Test object:type=<type> filter.

test_expect_success 'setup object-type' '
	test_create_repo object-type &&
	test_commit --no-tag -C object-type message blob &&
	git -C object-type tag tag -m tag-message
'

test_expect_success 'verify object:type= fails with invalid type' '
	test_must_fail git -C object-type rev-list --objects --filter=object:type= HEAD &&
	test_must_fail git -C object-type rev-list --objects --filter=object:type=invalid HEAD
'

test_expect_success 'verify object:type=blob prints blob and commit' '
	git -C object-type rev-parse HEAD >expected &&
	printf "%s blob\n" $(git -C object-type rev-parse HEAD:blob) >>expected &&
	git -C object-type rev-list --objects --filter=object:type=blob HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'verify object:type=tree prints tree and commit' '
	(
		git -C object-type rev-parse HEAD &&
		printf "%s \n" $(git -C object-type rev-parse HEAD^{tree})
	) >expected &&
	git -C object-type rev-list --objects --filter=object:type=tree HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'verify object:type=commit prints commit' '
	git -C object-type rev-parse HEAD >expected &&
	git -C object-type rev-list --objects --filter=object:type=commit HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'verify object:type=tag prints tag' '
	(
		git -C object-type rev-parse HEAD &&
		printf "%s tag\n" $(git -C object-type rev-parse tag)
	) >expected &&
	git -C object-type rev-list --objects --filter=object:type=tag tag >actual &&
	test_cmp expected actual
'

test_expect_success 'verify object:type=blob prints only blob with --filter-provided-objects' '
	printf "%s blob\n" $(git -C object-type rev-parse HEAD:blob) >expected &&
	git -C object-type rev-list --objects \
		--filter=object:type=blob --filter-provided-objects HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'verify object:type=tree prints only tree with --filter-provided-objects' '
	printf "%s \n" $(git -C object-type rev-parse HEAD^{tree}) >expected &&
	git -C object-type rev-list --objects \
		--filter=object:type=tree HEAD --filter-provided-objects >actual &&
	test_cmp expected actual
'

test_expect_success 'verify object:type=commit prints only commit with --filter-provided-objects' '
	git -C object-type rev-parse HEAD >expected &&
	git -C object-type rev-list --objects \
		--filter=object:type=commit --filter-provided-objects HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'verify object:type=tag prints only tag with --filter-provided-objects' '
	printf "%s tag\n" $(git -C object-type rev-parse tag) >expected &&
	git -C object-type rev-list --objects \
		--filter=object:type=tag --filter-provided-objects tag >actual &&
	test_cmp expected actual
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
		--filter=sparse:oid=main:pattern HEAD >revs &&
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
	! grep "Skipping contents of tree [^.]" filter_trace &&

	# Try this again with "combine:". If both sub-filters are skipping
	# trees, the composite filter should also skip trees. This is not
	# important unless the user does combine:tree:X+tree:Y or another filter
	# besides "tree:" is implemented in the future which can skip trees.
	GIT_TRACE=1 git -C r3 rev-list \
		--objects --filter=combine:tree:1+tree:3 HEAD 2>filter_trace &&

	# Only skip the dir1/ tree, which is shared between the two commits.
	grep "Skipping contents of tree " filter_trace >actual &&
	test_write_lines "Skipping contents of tree dir1/..." >expected &&
	test_cmp expected actual
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

test_expect_success 'combine:... for a simple combination' '
	git -C r3 rev-list --objects --filter=combine:tree:2+blob:none HEAD \
		>actual &&

	expect_has HEAD "" &&
	expect_has HEAD~1 "" &&
	expect_has HEAD dir1 &&

	# There are also 2 commit objects
	test_line_count = 5 actual &&

	cp actual expected &&

	# Try again using repeated --filter - this is equivalent to a manual
	# combine with "combine:...+..."
	git -C r3 rev-list --objects --filter=combine:tree:2 \
		--filter=blob:none HEAD >actual &&

	test_cmp expected actual
'

test_expect_success 'combine:... with URL encoding' '
	git -C r3 rev-list --objects \
		--filter=combine:tree%3a2+blob:%6Eon%65 HEAD >actual &&

	expect_has HEAD "" &&
	expect_has HEAD~1 "" &&
	expect_has HEAD dir1 &&

	# There are also 2 commit objects
	test_line_count = 5 actual
'

expect_invalid_filter_spec () {
	spec="$1" &&
	err="$2" &&

	test_must_fail git -C r3 rev-list --objects --filter="$spec" HEAD \
		>actual 2>actual_stderr &&
	test_must_be_empty actual &&
	test_i18ngrep "$err" actual_stderr
}

test_expect_success 'combine:... while URL-encoding things that should not be' '
	expect_invalid_filter_spec combine%3Atree:2+blob:none \
		"invalid filter-spec"
'

test_expect_success 'combine: with nothing after the :' '
	expect_invalid_filter_spec combine: "expected something after combine:"
'

test_expect_success 'parse error in first sub-filter in combine:' '
	expect_invalid_filter_spec combine:tree:asdf+blob:none \
		"expected .tree:<depth>."
'

test_expect_success 'combine:... with non-encoded reserved chars' '
	expect_invalid_filter_spec combine:tree:2+sparse:@xyz \
		"must escape char in sub-filter-spec: .@." &&
	expect_invalid_filter_spec combine:tree:2+sparse:\` \
		"must escape char in sub-filter-spec: .\`." &&
	expect_invalid_filter_spec combine:tree:2+sparse:~abc \
		"must escape char in sub-filter-spec: .\~."
'

test_expect_success 'validate err msg for "combine:<valid-filter>+"' '
	expect_invalid_filter_spec combine:tree:2+ "expected .tree:<depth>."
'

test_expect_success 'combine:... with edge-case hex digits: Ff Aa 0 9' '
	git -C r3 rev-list --objects --filter="combine:tree:2+bl%6Fb:n%6fne" \
		HEAD >actual &&
	test_line_count = 5 actual &&
	git -C r3 rev-list --objects --filter="combine:tree%3A2+blob%3anone" \
		HEAD >actual &&
	test_line_count = 5 actual &&
	git -C r3 rev-list --objects --filter="combine:tree:%30" HEAD >actual &&
	test_line_count = 2 actual &&
	git -C r3 rev-list --objects --filter="combine:tree:%39+blob:none" \
		HEAD >actual &&
	test_line_count = 5 actual
'

test_expect_success 'add sparse pattern blobs whose paths have reserved chars' '
	cp r3/pattern r3/pattern1+renamed% &&
	cp r3/pattern "r3/p;at%ter+n" &&
	cp r3/pattern r3/^~pattern &&
	git -C r3 add pattern1+renamed% "p;at%ter+n" ^~pattern &&
	git -C r3 commit -m "add sparse pattern files with reserved chars"
'

test_expect_success 'combine:... with more than two sub-filters' '
	git -C r3 rev-list --objects \
		--filter=combine:tree:3+blob:limit=40+sparse:oid=main:pattern \
		HEAD >actual &&

	expect_has HEAD "" &&
	expect_has HEAD~1 "" &&
	expect_has HEAD~2 "" &&
	expect_has HEAD dir1 &&
	expect_has HEAD dir1/sparse1 &&
	expect_has HEAD dir1/sparse2 &&

	# Should also have 3 commits
	test_line_count = 9 actual &&

	# Try again, this time making sure the last sub-filter is only
	# URL-decoded once.
	cp actual expect &&

	git -C r3 rev-list --objects \
		--filter=combine:tree:3+blob:limit=40+sparse:oid=main:pattern1%2brenamed%25 \
		HEAD >actual &&
	test_cmp expect actual &&

	# Use the same composite filter again, but with a pattern file name that
	# requires encoding multiple characters, and use implicit filter
	# combining.
	test_when_finished "rm -f trace1" &&
	GIT_TRACE=$(pwd)/trace1 git -C r3 rev-list --objects \
		--filter=tree:3 --filter=blob:limit=40 \
		--filter=sparse:oid="main:p;at%ter+n" \
		HEAD >actual &&

	test_cmp expect actual &&
	grep "Add to combine filter-spec: sparse:oid=main:p%3bat%25ter%2bn" \
		trace1 &&

	# Repeat the above test, but this time, the characters to encode are in
	# the LHS of the combined filter.
	test_when_finished "rm -f trace2" &&
	GIT_TRACE=$(pwd)/trace2 git -C r3 rev-list --objects \
		--filter=sparse:oid=main:^~pattern \
		--filter=tree:3 --filter=blob:limit=40 \
		HEAD >actual &&

	test_cmp expect actual &&
	grep "Add to combine filter-spec: sparse:oid=main:%5e%7epattern" \
		trace2
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

test_expect_success 'setup r5' '
	git init r5 &&
	mkdir -p r5/subdir &&

	echo 1     >r5/short-root          &&
	echo 12345 >r5/long-root           &&
	echo a     >r5/subdir/short-subdir &&
	echo abcde >r5/subdir/long-subdir  &&

	git -C r5 add short-root long-root subdir &&
	git -C r5 commit -m "commit msg"
'

test_expect_success 'verify collecting omits in combined: filter' '
	# Note that this test guards against the naive implementation of simply
	# giving both filters the same "omits" set and expecting it to
	# automatically merge them.
	git -C r5 rev-list --objects --quiet --filter-print-omitted \
		--filter=combine:tree:2+blob:limit=3 HEAD >actual &&

	# Expect 0 trees/commits, 3 blobs omitted (all blobs except short-root)
	omitted_1=$(echo 12345 | git hash-object --stdin) &&
	omitted_2=$(echo a     | git hash-object --stdin) &&
	omitted_3=$(echo abcde | git hash-object --stdin) &&

	grep ~$omitted_1 actual &&
	grep ~$omitted_2 actual &&
	grep ~$omitted_3 actual &&
	test_line_count = 3 actual
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
		rm r1/.git/objects/$id || return 1
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

test_done
