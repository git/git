#!/bin/sh

# NOTICE:
#   This testsuite does a number of diffs and checks that the output match.
#   However, it is a "garbage in, garbage out" situation; the trees have
#   duplicate entries for individual paths, and it results in diffs that do
#   not make much sense.  As such, it is not clear that the diffs are
#   "correct".  The primary purpose of these tests was to verify that
#   diff-tree does not segfault, but there is perhaps some value in ensuring
#   that the diff output isn't wildly unreasonable.

test_description='test tree diff when trees have duplicate entries'

. ./test-lib.sh

# make_tree_entry <mode> <mode> <sha1>
#
# We have to rely on perl here because not all printfs understand
# hex escapes (only octal), and xxd is not portable.
make_tree_entry () {
	printf '%s %s\0' "$1" "$2" &&
	perl -e 'print chr(hex($_)) for ($ARGV[0] =~ /../g)' "$3"
}

# Like git-mktree, but without all of the pesky sanity checking.
# Arguments come in groups of three, each group specifying a single
# tree entry (see make_tree_entry above).
make_tree () {
	while test $# -gt 2; do
		make_tree_entry "$1" "$2" "$3"
		shift; shift; shift
	done |
	git hash-object --literally -w -t tree --stdin
}

# this is kind of a convoluted setup, but matches
# a real-world case. Each tree contains four entries
# for the given path, one with one sha1, and three with
# the other. The first tree has them split across
# two subtrees (which are themselves duplicate entries in
# the root tree), and the second has them all in a single subtree.
test_expect_success 'create trees with duplicate entries' '
	blob_one=$(echo one | git hash-object -w --stdin) &&
	blob_two=$(echo two | git hash-object -w --stdin) &&
	inner_one_a=$(make_tree \
		100644 inner $blob_one
	) &&
	inner_one_b=$(make_tree \
		100644 inner $blob_two \
		100644 inner $blob_two \
		100644 inner $blob_two
	) &&
	outer_one=$(make_tree \
		040000 outer $inner_one_a \
		040000 outer $inner_one_b
	) &&
	inner_two=$(make_tree \
		100644 inner $blob_one \
		100644 inner $blob_two \
		100644 inner $blob_two \
		100644 inner $blob_two
	) &&
	outer_two=$(make_tree \
		040000 outer $inner_two
	) &&
	git tag one $outer_one &&
	git tag two $outer_two
'

test_expect_success 'create tree without duplicate entries' '
	blob_one=$(echo one | git hash-object -w --stdin) &&
	outer_three=$(make_tree \
		100644 renamed $blob_one
	) &&
	git tag three $outer_three
'

test_expect_success 'diff-tree between duplicate trees' '
	# See NOTICE at top of file
	{
		printf ":000000 100644 $ZERO_OID $blob_two A\touter/inner\n" &&
		printf ":000000 100644 $ZERO_OID $blob_two A\touter/inner\n" &&
		printf ":000000 100644 $ZERO_OID $blob_two A\touter/inner\n" &&
		printf ":100644 000000 $blob_two $ZERO_OID D\touter/inner\n" &&
		printf ":100644 000000 $blob_two $ZERO_OID D\touter/inner\n" &&
		printf ":100644 000000 $blob_two $ZERO_OID D\touter/inner\n"
	} >expect &&
	git diff-tree -r --no-abbrev one two >actual &&
	test_cmp expect actual
'

test_expect_success 'diff-tree with renames' '
	# See NOTICE at top of file.
	git diff-tree -M -r --no-abbrev one two >actual &&
	test_must_be_empty actual
'

test_expect_success 'diff-tree FROM duplicate tree' '
	# See NOTICE at top of file.
	{
		printf ":100644 000000 $blob_one $ZERO_OID D\touter/inner\n" &&
		printf ":100644 000000 $blob_two $ZERO_OID D\touter/inner\n" &&
		printf ":100644 000000 $blob_two $ZERO_OID D\touter/inner\n" &&
		printf ":100644 000000 $blob_two $ZERO_OID D\touter/inner\n" &&
		printf ":000000 100644 $ZERO_OID $blob_one A\trenamed\n"
	} >expect &&
	git diff-tree -r --no-abbrev one three >actual &&
	test_cmp expect actual
'

test_expect_success 'diff-tree FROM duplicate tree, with renames' '
	# See NOTICE at top of file.
	{
		printf ":100644 000000 $blob_two $ZERO_OID D\touter/inner\n" &&
		printf ":100644 000000 $blob_two $ZERO_OID D\touter/inner\n" &&
		printf ":100644 000000 $blob_two $ZERO_OID D\touter/inner\n" &&
		printf ":100644 100644 $blob_one $blob_one R100\touter/inner\trenamed\n"
	} >expect &&
	git diff-tree -M -r --no-abbrev one three >actual &&
	test_cmp expect actual
'

test_expect_success 'create a few commits' '
	git commit-tree -m "Duplicate Entries" two^{tree} >commit_id &&
	git branch base $(cat commit_id) &&

	git commit-tree -p $(cat commit_id) -m "Just one" three^{tree} >up &&
	git branch update $(cat up) &&

	git commit-tree -p $(cat up) -m "Back to weird" two^{tree} >final &&
	git branch final $(cat final) &&

	rm commit_id up final
'

test_expect_success 'git read-tree does not segfault' '
	test_must_fail git read-tree --reset base 2>err &&
	test_grep "error: corrupted cache-tree has entries not present in index" err
'

test_expect_success 'reset --hard does not segfault' '
	git checkout base &&
	test_must_fail git reset --hard 2>err &&
	test_grep "error: corrupted cache-tree has entries not present in index" err
'

test_expect_success 'git diff HEAD does not segfault' '
	git checkout base &&
	GIT_TEST_CHECK_CACHE_TREE=false &&
	git reset --hard &&
	test_must_fail git diff HEAD 2>err &&
	test_grep "error: corrupted cache-tree has entries not present in index" err
'

test_expect_failure 'can switch to another branch when status is empty' '
	git clean -ffdqx &&
	git status --porcelain -uno >actual &&
	test_must_be_empty actual &&
	git checkout update
'

test_expect_success 'forcibly switch to another branch, verify status empty' '
	git checkout -f update &&
	git status --porcelain -uno >actual &&
	test_must_be_empty actual
'

test_expect_success 'fast-forward from non-duplicate entries to duplicate' '
	git merge final
'

test_expect_failure 'clean status, switch branches, status still clean' '
	git status --porcelain -uno >actual &&
	test_must_be_empty actual &&
	git checkout base &&
	git status --porcelain -uno >actual &&
	test_must_be_empty actual
'

test_expect_success 'switch to base branch and force status to be clean' '
	git checkout base &&
	GIT_TEST_CHECK_CACHE_TREE=false git reset --hard &&
	git status --porcelain -uno >actual &&
	test_must_be_empty actual
'

test_expect_failure 'fast-forward from duplicate entries to non-duplicate' '
	git merge update
'

test_done
