#!/bin/sh

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
	git hash-object -w -t tree --stdin
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

test_expect_success 'diff-tree between trees' '
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
	# same expectation as above, since we disable rename detection
	git diff-tree -M -r --no-abbrev one two >actual &&
	test_cmp expect actual
'

test_done
