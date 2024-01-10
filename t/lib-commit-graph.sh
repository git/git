#!/bin/sh

# Helper functions for testing commit-graphs.

# Initialize OID cache with oid_version
test_oid_cache <<-EOF
oid_version sha1:1
oid_version sha256:2
EOF

graph_git_two_modes() {
	git -c core.commitGraph=true $1 >output &&
	git -c core.commitGraph=false $1 >expect &&
	test_cmp expect output
}

# graph_git_behavior <name> <directory> <branch> <compare>
#
# Ensures that a handful of traversal operations produce the same
# results with and without the commit-graph in use.
#
# NOTE: it is a bug to call this function with <directory> containing
# any characters in $IFS.
graph_git_behavior() {
	MSG=$1
	DIR=$2
	BRANCH=$3
	COMPARE=$4
	test_expect_success "check normal git operations: $MSG" '
		graph_git_two_modes "${DIR:+-C $DIR} log --oneline $BRANCH" &&
		graph_git_two_modes "${DIR:+-C $DIR} log --topo-order $BRANCH" &&
		graph_git_two_modes "${DIR:+-C $DIR} log --graph $COMPARE..$BRANCH" &&
		graph_git_two_modes "${DIR:+-C $DIR} branch -vv" &&
		graph_git_two_modes "${DIR:+-C $DIR} merge-base -a $BRANCH $COMPARE"
	'
}

graph_read_expect() {
	OPTIONAL=""
	NUM_CHUNKS=3
	DIR="."
	if test "$1" = -C
	then
		shift
		DIR="$1"
		shift
	fi
	if test -n "$2"
	then
		OPTIONAL=" $2"
		NUM_CHUNKS=$((3 + $(echo "$2" | wc -w)))
	fi
	GENERATION_VERSION=2
	if test -n "$3"
	then
		GENERATION_VERSION=$3
	fi
	OPTIONS=
	if test $GENERATION_VERSION -gt 1
	then
		OPTIONS=" read_generation_data"
	fi
	cat >"$DIR/expect" <<-EOF
	header: 43475048 1 $(test_oid oid_version) $NUM_CHUNKS 0
	num_commits: $1
	chunks: oid_fanout oid_lookup commit_metadata$OPTIONAL
	options:$OPTIONS
	EOF
	(
		cd "$DIR" &&
		test-tool read-graph >output &&
		test_cmp expect output
	)
}
