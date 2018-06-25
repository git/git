#!/bin/sh

test_description='commit graph'
. ./test-lib.sh

test_expect_success 'setup full repo' '
	mkdir full &&
	cd "$TRASH_DIRECTORY/full" &&
	git init &&
	git config core.commitGraph true &&
	objdir=".git/objects"
'

test_expect_success 'write graph with no packs' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write --object-dir . &&
	test_path_is_file info/commit-graph
'

test_expect_success 'create commits and repack' '
	cd "$TRASH_DIRECTORY/full" &&
	for i in $(test_seq 3)
	do
		test_commit $i &&
		git branch commits/$i
	done &&
	git repack
'

graph_git_two_modes() {
	git -c core.graph=true $1 >output
	git -c core.graph=false $1 >expect
	test_cmp output expect
}

graph_git_behavior() {
	MSG=$1
	DIR=$2
	BRANCH=$3
	COMPARE=$4
	test_expect_success "check normal git operations: $MSG" '
		cd "$TRASH_DIRECTORY/$DIR" &&
		graph_git_two_modes "log --oneline $BRANCH" &&
		graph_git_two_modes "log --topo-order $BRANCH" &&
		graph_git_two_modes "log --graph $COMPARE..$BRANCH" &&
		graph_git_two_modes "branch -vv" &&
		graph_git_two_modes "merge-base -a $BRANCH $COMPARE"
	'
}

graph_git_behavior 'no graph' full commits/3 commits/1

graph_read_expect() {
	OPTIONAL=""
	NUM_CHUNKS=3
	if test ! -z $2
	then
		OPTIONAL=" $2"
		NUM_CHUNKS=$((3 + $(echo "$2" | wc -w)))
	fi
	cat >expect <<- EOF
	header: 43475048 1 1 $NUM_CHUNKS 0
	num_commits: $1
	chunks: oid_fanout oid_lookup commit_metadata$OPTIONAL
	EOF
	git commit-graph read >output &&
	test_cmp expect output
}

test_expect_success 'write graph' '
	cd "$TRASH_DIRECTORY/full" &&
	graph1=$(git commit-graph write) &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "3"
'

graph_git_behavior 'graph exists' full commits/3 commits/1

test_expect_success 'Add more commits' '
	cd "$TRASH_DIRECTORY/full" &&
	git reset --hard commits/1 &&
	for i in $(test_seq 4 5)
	do
		test_commit $i &&
		git branch commits/$i
	done &&
	git reset --hard commits/2 &&
	for i in $(test_seq 6 7)
	do
		test_commit $i &&
		git branch commits/$i
	done &&
	git reset --hard commits/2 &&
	git merge commits/4 &&
	git branch merge/1 &&
	git reset --hard commits/4 &&
	git merge commits/6 &&
	git branch merge/2 &&
	git reset --hard commits/3 &&
	git merge commits/5 commits/7 &&
	git branch merge/3 &&
	git repack
'

# Current graph structure:
#
#   __M3___
#  /   |   \
# 3 M1 5 M2 7
# |/  \|/  \|
# 2    4    6
# |___/____/
# 1

test_expect_success 'write graph with merges' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "10" "large_edges"
'

graph_git_behavior 'merge 1 vs 2' full merge/1 merge/2
graph_git_behavior 'merge 1 vs 3' full merge/1 merge/3
graph_git_behavior 'merge 2 vs 3' full merge/2 merge/3

test_expect_success 'Add one more commit' '
	cd "$TRASH_DIRECTORY/full" &&
	test_commit 8 &&
	git branch commits/8 &&
	ls $objdir/pack | grep idx >existing-idx &&
	git repack &&
	ls $objdir/pack| grep idx | grep -v --file=existing-idx >new-idx
'

# Current graph structure:
#
#      8
#      |
#   __M3___
#  /   |   \
# 3 M1 5 M2 7
# |/  \|/  \|
# 2    4    6
# |___/____/
# 1

graph_git_behavior 'mixed mode, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'mixed mode, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'write graph with new commit' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "11" "large_edges"
'

graph_git_behavior 'full graph, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'full graph, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'write graph with nothing new' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "11" "large_edges"
'

graph_git_behavior 'cleared graph, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'cleared graph, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'build graph from latest pack with closure' '
	cd "$TRASH_DIRECTORY/full" &&
	cat new-idx | git commit-graph write --stdin-packs &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "9" "large_edges"
'

graph_git_behavior 'graph from pack, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'graph from pack, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'build graph from commits with closure' '
	cd "$TRASH_DIRECTORY/full" &&
	git tag -a -m "merge" tag/merge merge/2 &&
	git rev-parse tag/merge >commits-in &&
	git rev-parse merge/1 >>commits-in &&
	cat commits-in | git commit-graph write --stdin-commits &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "6"
'

graph_git_behavior 'graph from commits, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'graph from commits, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'build graph from commits with append' '
	cd "$TRASH_DIRECTORY/full" &&
	git rev-parse merge/3 | git commit-graph write --stdin-commits --append &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "10" "large_edges"
'

graph_git_behavior 'append graph, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'append graph, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'setup bare repo' '
	cd "$TRASH_DIRECTORY" &&
	git clone --bare --no-local full bare &&
	cd bare &&
	git config core.commitGraph true &&
	baredir="./objects"
'

graph_git_behavior 'bare repo, commit 8 vs merge 1' bare commits/8 merge/1
graph_git_behavior 'bare repo, commit 8 vs merge 2' bare commits/8 merge/2

test_expect_success 'write graph in bare repo' '
	cd "$TRASH_DIRECTORY/bare" &&
	git commit-graph write &&
	test_path_is_file $baredir/info/commit-graph &&
	graph_read_expect "11" "large_edges"
'

graph_git_behavior 'bare repo with graph, commit 8 vs merge 1' bare commits/8 merge/1
graph_git_behavior 'bare repo with graph, commit 8 vs merge 2' bare commits/8 merge/2

test_expect_success 'perform fast-forward merge in full repo' '
	cd "$TRASH_DIRECTORY/full" &&
	git checkout -b merge-5-to-8 commits/5 &&
	git merge commits/8 &&
	git show-ref -s merge-5-to-8 >output &&
	git show-ref -s commits/8 >expect &&
	test_cmp expect output
'

test_done
