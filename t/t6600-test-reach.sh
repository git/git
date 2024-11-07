#!/bin/sh

test_description='basic commit reachability tests'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# Construct a grid-like commit graph with points (x,y)
# with 1 <= x <= 10, 1 <= y <= 10, where (x,y) has
# parents (x-1, y) and (x, y-1), keeping in mind that
# we drop a parent if a coordinate is nonpositive.
#
#             (10,10)
#            /       \
#         (10,9)    (9,10)
#        /     \   /      \
#    (10,8)    (9,9)      (8,10)
#   /     \    /   \      /    \
#         ( continued...)
#   \     /    \   /      \    /
#    (3,1)     (2,2)      (1,3)
#        \     /    \     /
#         (2,1)      (2,1)
#              \    /
#              (1,1)
#
# We use branch 'commit-x-y' to refer to (x,y).
# This grid allows interesting reachability and
# non-reachability queries: (x,y) can reach (x',y')
# if and only if x' <= x and y' <= y.
test_expect_success 'setup' '
	for i in $(test_seq 1 10)
	do
		test_commit "1-$i" &&
		git branch -f commit-1-$i &&
		git tag -a -m "1-$i" tag-1-$i commit-1-$i || return 1
	done &&
	for j in $(test_seq 1 9)
	do
		git reset --hard commit-$j-1 &&
		x=$(($j + 1)) &&
		test_commit "$x-1" &&
		git branch -f commit-$x-1 &&
		git tag -a -m "$x-1" tag-$x-1 commit-$x-1 &&

		for i in $(test_seq 2 10)
		do
			git merge commit-$j-$i -m "$x-$i" &&
			git branch -f commit-$x-$i &&
			git tag -a -m "$x-$i" tag-$x-$i commit-$x-$i || return 1
		done
	done &&
	git commit-graph write --reachable &&
	mv .git/objects/info/commit-graph commit-graph-full &&
	chmod u+w commit-graph-full &&
	git show-ref -s commit-5-5 | git commit-graph write --stdin-commits &&
	mv .git/objects/info/commit-graph commit-graph-half &&
	chmod u+w commit-graph-half &&
	git -c commitGraph.generationVersion=1 commit-graph write --reachable &&
	mv .git/objects/info/commit-graph commit-graph-no-gdat &&
	chmod u+w commit-graph-no-gdat &&
	git config core.commitGraph true
'

run_all_modes () {
	test_when_finished rm -rf .git/objects/info/commit-graph &&
	"$@" <input >actual &&
	test_cmp expect actual &&
	cp commit-graph-full .git/objects/info/commit-graph &&
	"$@" <input >actual &&
	test_cmp expect actual &&
	cp commit-graph-half .git/objects/info/commit-graph &&
	"$@" <input >actual &&
	test_cmp expect actual &&
	cp commit-graph-no-gdat .git/objects/info/commit-graph &&
	"$@" <input >actual &&
	test_cmp expect actual
}

test_all_modes () {
	run_all_modes test-tool reach "$@"
}

test_expect_success 'ref_newer:miss' '
	cat >input <<-\EOF &&
	A:commit-5-7
	B:commit-4-9
	EOF
	echo "ref_newer(A,B):0" >expect &&
	test_all_modes ref_newer
'

test_expect_success 'ref_newer:hit' '
	cat >input <<-\EOF &&
	A:commit-5-7
	B:commit-2-3
	EOF
	echo "ref_newer(A,B):1" >expect &&
	test_all_modes ref_newer
'

test_expect_success 'in_merge_bases:hit' '
	cat >input <<-\EOF &&
	A:commit-5-7
	B:commit-8-8
	EOF
	echo "in_merge_bases(A,B):1" >expect &&
	test_all_modes in_merge_bases
'

test_expect_success 'in_merge_bases:miss' '
	cat >input <<-\EOF &&
	A:commit-6-8
	B:commit-5-9
	EOF
	echo "in_merge_bases(A,B):0" >expect &&
	test_all_modes in_merge_bases
'

test_expect_success 'in_merge_bases_many:hit' '
	cat >input <<-\EOF &&
	A:commit-6-8
	X:commit-6-9
	X:commit-5-7
	EOF
	echo "in_merge_bases_many(A,X):1" >expect &&
	test_all_modes in_merge_bases_many
'

test_expect_success 'in_merge_bases_many:miss' '
	cat >input <<-\EOF &&
	A:commit-6-8
	X:commit-7-7
	X:commit-8-6
	EOF
	echo "in_merge_bases_many(A,X):0" >expect &&
	test_all_modes in_merge_bases_many
'

test_expect_success 'in_merge_bases_many:miss-heuristic' '
	cat >input <<-\EOF &&
	A:commit-6-8
	X:commit-7-5
	X:commit-6-6
	EOF
	echo "in_merge_bases_many(A,X):0" >expect &&
	test_all_modes in_merge_bases_many
'

test_expect_success 'is_descendant_of:hit' '
	cat >input <<-\EOF &&
	A:commit-5-7
	X:commit-4-8
	X:commit-6-6
	X:commit-1-1
	EOF
	echo "is_descendant_of(A,X):1" >expect &&
	test_all_modes is_descendant_of
'

test_expect_success 'is_descendant_of:miss' '
	cat >input <<-\EOF &&
	A:commit-6-8
	X:commit-5-9
	X:commit-4-10
	X:commit-7-6
	EOF
	echo "is_descendant_of(A,X):0" >expect &&
	test_all_modes is_descendant_of
'

test_expect_success 'get_merge_bases_many' '
	cat >input <<-\EOF &&
	A:commit-5-7
	X:commit-4-8
	X:commit-6-6
	X:commit-8-3
	EOF
	{
		echo "get_merge_bases_many(A,X):" &&
		git rev-parse commit-5-6 \
			      commit-4-7 | sort
	} >expect &&
	test_all_modes get_merge_bases_many
'

test_expect_success 'reduce_heads' '
	cat >input <<-\EOF &&
	X:commit-1-10
	X:commit-2-8
	X:commit-3-6
	X:commit-4-4
	X:commit-1-7
	X:commit-2-5
	X:commit-3-3
	X:commit-5-1
	EOF
	{
		echo "reduce_heads(X):" &&
		git rev-parse commit-5-1 \
			      commit-4-4 \
			      commit-3-6 \
			      commit-2-8 \
			      commit-1-10 | sort
	} >expect &&
	test_all_modes reduce_heads
'

test_expect_success 'can_all_from_reach:hit' '
	cat >input <<-\EOF &&
	X:commit-2-10
	X:commit-3-9
	X:commit-4-8
	X:commit-5-7
	X:commit-6-6
	X:commit-7-5
	X:commit-8-4
	X:commit-9-3
	Y:commit-1-9
	Y:commit-2-8
	Y:commit-3-7
	Y:commit-4-6
	Y:commit-5-5
	Y:commit-6-4
	Y:commit-7-3
	Y:commit-8-1
	EOF
	echo "can_all_from_reach(X,Y):1" >expect &&
	test_all_modes can_all_from_reach
'

test_expect_success 'can_all_from_reach:miss' '
	cat >input <<-\EOF &&
	X:commit-2-10
	X:commit-3-9
	X:commit-4-8
	X:commit-5-7
	X:commit-6-6
	X:commit-7-5
	X:commit-8-4
	X:commit-9-3
	Y:commit-1-9
	Y:commit-2-8
	Y:commit-3-7
	Y:commit-4-6
	Y:commit-5-5
	Y:commit-6-4
	Y:commit-8-5
	EOF
	echo "can_all_from_reach(X,Y):0" >expect &&
	test_all_modes can_all_from_reach
'

test_expect_success 'can_all_from_reach_with_flag: tags case' '
	cat >input <<-\EOF &&
	X:tag-2-10
	X:tag-3-9
	X:tag-4-8
	X:commit-5-7
	X:commit-6-6
	X:commit-7-5
	X:commit-8-4
	X:commit-9-3
	Y:tag-1-9
	Y:tag-2-8
	Y:tag-3-7
	Y:commit-4-6
	Y:commit-5-5
	Y:commit-6-4
	Y:commit-7-3
	Y:commit-8-1
	EOF
	echo "can_all_from_reach_with_flag(X,_,_,0,0):1" >expect &&
	test_all_modes can_all_from_reach_with_flag
'

test_expect_success 'commit_contains:hit' '
	cat >input <<-\EOF &&
	A:commit-7-7
	X:commit-2-10
	X:commit-3-9
	X:commit-4-8
	X:commit-5-7
	X:commit-6-6
	X:commit-7-5
	X:commit-8-4
	X:commit-9-3
	EOF
	echo "commit_contains(_,A,X,_):1" >expect &&
	test_all_modes commit_contains &&
	test_all_modes commit_contains --tag
'

test_expect_success 'commit_contains:miss' '
	cat >input <<-\EOF &&
	A:commit-6-5
	X:commit-2-10
	X:commit-3-9
	X:commit-4-8
	X:commit-5-7
	X:commit-6-6
	X:commit-7-5
	X:commit-8-4
	X:commit-9-3
	EOF
	echo "commit_contains(_,A,X,_):0" >expect &&
	test_all_modes commit_contains &&
	test_all_modes commit_contains --tag
'

test_expect_success 'rev-list: basic topo-order' '
	git rev-parse \
		commit-6-6 commit-5-6 commit-4-6 commit-3-6 commit-2-6 commit-1-6 \
		commit-6-5 commit-5-5 commit-4-5 commit-3-5 commit-2-5 commit-1-5 \
		commit-6-4 commit-5-4 commit-4-4 commit-3-4 commit-2-4 commit-1-4 \
		commit-6-3 commit-5-3 commit-4-3 commit-3-3 commit-2-3 commit-1-3 \
		commit-6-2 commit-5-2 commit-4-2 commit-3-2 commit-2-2 commit-1-2 \
		commit-6-1 commit-5-1 commit-4-1 commit-3-1 commit-2-1 commit-1-1 \
	>expect &&
	run_all_modes git rev-list --topo-order commit-6-6
'

test_expect_success 'rev-list: first-parent topo-order' '
	git rev-parse \
		commit-6-6 \
		commit-6-5 \
		commit-6-4 \
		commit-6-3 \
		commit-6-2 \
		commit-6-1 commit-5-1 commit-4-1 commit-3-1 commit-2-1 commit-1-1 \
	>expect &&
	run_all_modes git rev-list --first-parent --topo-order commit-6-6
'

test_expect_success 'rev-list: range topo-order' '
	git rev-parse \
		commit-6-6 commit-5-6 commit-4-6 commit-3-6 commit-2-6 commit-1-6 \
		commit-6-5 commit-5-5 commit-4-5 commit-3-5 commit-2-5 commit-1-5 \
		commit-6-4 commit-5-4 commit-4-4 commit-3-4 commit-2-4 commit-1-4 \
		commit-6-3 commit-5-3 commit-4-3 \
		commit-6-2 commit-5-2 commit-4-2 \
		commit-6-1 commit-5-1 commit-4-1 \
	>expect &&
	run_all_modes git rev-list --topo-order commit-3-3..commit-6-6
'

test_expect_success 'rev-list: range topo-order' '
	git rev-parse \
		commit-6-6 commit-5-6 commit-4-6 \
		commit-6-5 commit-5-5 commit-4-5 \
		commit-6-4 commit-5-4 commit-4-4 \
		commit-6-3 commit-5-3 commit-4-3 \
		commit-6-2 commit-5-2 commit-4-2 \
		commit-6-1 commit-5-1 commit-4-1 \
	>expect &&
	run_all_modes git rev-list --topo-order commit-3-8..commit-6-6
'

test_expect_success 'rev-list: first-parent range topo-order' '
	git rev-parse \
		commit-6-6 \
		commit-6-5 \
		commit-6-4 \
		commit-6-3 \
		commit-6-2 \
		commit-6-1 commit-5-1 commit-4-1 \
	>expect &&
	run_all_modes git rev-list --first-parent --topo-order commit-3-8..commit-6-6
'

test_expect_success 'rev-list: ancestry-path topo-order' '
	git rev-parse \
		commit-6-6 commit-5-6 commit-4-6 commit-3-6 \
		commit-6-5 commit-5-5 commit-4-5 commit-3-5 \
		commit-6-4 commit-5-4 commit-4-4 commit-3-4 \
		commit-6-3 commit-5-3 commit-4-3 \
	>expect &&
	run_all_modes git rev-list --topo-order --ancestry-path commit-3-3..commit-6-6
'

test_expect_success 'rev-list: symmetric difference topo-order' '
	git rev-parse \
		commit-6-6 commit-5-6 commit-4-6 \
		commit-6-5 commit-5-5 commit-4-5 \
		commit-6-4 commit-5-4 commit-4-4 \
		commit-6-3 commit-5-3 commit-4-3 \
		commit-6-2 commit-5-2 commit-4-2 \
		commit-6-1 commit-5-1 commit-4-1 \
		commit-3-8 commit-2-8 commit-1-8 \
		commit-3-7 commit-2-7 commit-1-7 \
	>expect &&
	run_all_modes git rev-list --topo-order commit-3-8...commit-6-6
'

test_expect_success 'get_reachable_subset:all' '
	cat >input <<-\EOF &&
	X:commit-9-1
	X:commit-8-3
	X:commit-7-5
	X:commit-6-6
	X:commit-1-7
	Y:commit-3-3
	Y:commit-1-7
	Y:commit-5-6
	EOF
	(
		echo "get_reachable_subset(X,Y)" &&
		git rev-parse commit-3-3 \
			      commit-1-7 \
			      commit-5-6 | sort
	) >expect &&
	test_all_modes get_reachable_subset
'

test_expect_success 'get_reachable_subset:some' '
	cat >input <<-\EOF &&
	X:commit-9-1
	X:commit-8-3
	X:commit-7-5
	X:commit-1-7
	Y:commit-3-3
	Y:commit-1-7
	Y:commit-5-6
	EOF
	(
		echo "get_reachable_subset(X,Y)" &&
		git rev-parse commit-3-3 \
			      commit-1-7 | sort
	) >expect &&
	test_all_modes get_reachable_subset
'

test_expect_success 'get_reachable_subset:none' '
	cat >input <<-\EOF &&
	X:commit-9-1
	X:commit-8-3
	X:commit-7-5
	X:commit-1-7
	Y:commit-9-3
	Y:commit-7-6
	Y:commit-2-8
	EOF
	echo "get_reachable_subset(X,Y)" >expect &&
	test_all_modes get_reachable_subset
'

test_expect_success 'for-each-ref ahead-behind:linear' '
	cat >input <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-1-3
	refs/heads/commit-1-5
	refs/heads/commit-1-8
	EOF
	cat >expect <<-\EOF &&
	refs/heads/commit-1-1 0 8
	refs/heads/commit-1-3 0 6
	refs/heads/commit-1-5 0 4
	refs/heads/commit-1-8 0 1
	EOF
	run_all_modes git for-each-ref \
		--format="%(refname) %(ahead-behind:commit-1-9)" --stdin
'

test_expect_success 'for-each-ref ahead-behind:all' '
	cat >input <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-2-4
	refs/heads/commit-4-2
	refs/heads/commit-4-4
	EOF
	cat >expect <<-\EOF &&
	refs/heads/commit-1-1 0 24
	refs/heads/commit-2-4 0 17
	refs/heads/commit-4-2 0 17
	refs/heads/commit-4-4 0 9
	EOF
	run_all_modes git for-each-ref \
		--format="%(refname) %(ahead-behind:commit-5-5)" --stdin
'

test_expect_success 'for-each-ref ahead-behind:some' '
	cat >input <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-5-3
	refs/heads/commit-4-8
	refs/heads/commit-9-9
	EOF
	cat >expect <<-\EOF &&
	refs/heads/commit-1-1 0 53
	refs/heads/commit-4-8 8 30
	refs/heads/commit-5-3 0 39
	refs/heads/commit-9-9 27 0
	EOF
	run_all_modes git for-each-ref \
		--format="%(refname) %(ahead-behind:commit-9-6)" --stdin
'

test_expect_success 'for-each-ref ahead-behind:some, multibase' '
	cat >input <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-5-3
	refs/heads/commit-7-8
	refs/heads/commit-4-8
	refs/heads/commit-9-9
	EOF
	cat >expect <<-\EOF &&
	refs/heads/commit-1-1 0 53 0 53
	refs/heads/commit-4-8 8 30 0 22
	refs/heads/commit-5-3 0 39 0 39
	refs/heads/commit-7-8 14 12 8 6
	refs/heads/commit-9-9 27 0 27 0
	EOF
	run_all_modes git for-each-ref \
		--format="%(refname) %(ahead-behind:commit-9-6) %(ahead-behind:commit-6-9)" \
		--stdin
'

test_expect_success 'for-each-ref ahead-behind:none' '
	cat >input <<-\EOF &&
	refs/heads/commit-7-5
	refs/heads/commit-4-8
	refs/heads/commit-9-9
	EOF
	cat >expect <<-\EOF &&
	refs/heads/commit-4-8 16 16
	refs/heads/commit-7-5 7 4
	refs/heads/commit-9-9 49 0
	EOF
	run_all_modes git for-each-ref \
		--format="%(refname) %(ahead-behind:commit-8-4)" --stdin
'

test_expect_success 'for-each-ref merged:linear' '
	cat >input <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-1-3
	refs/heads/commit-1-5
	refs/heads/commit-1-8
	refs/heads/commit-2-1
	refs/heads/commit-5-1
	refs/heads/commit-9-1
	EOF
	cat >expect <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-1-3
	refs/heads/commit-1-5
	refs/heads/commit-1-8
	EOF
	run_all_modes git for-each-ref --merged=commit-1-9 \
		--format="%(refname)" --stdin
'

test_expect_success 'for-each-ref merged:all' '
	cat >input <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-2-4
	refs/heads/commit-4-2
	refs/heads/commit-4-4
	EOF
	cat >expect <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-2-4
	refs/heads/commit-4-2
	refs/heads/commit-4-4
	EOF
	run_all_modes git for-each-ref --merged=commit-5-5 \
		--format="%(refname)" --stdin
'

test_expect_success 'for-each-ref ahead-behind:some' '
	cat >input <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-5-3
	refs/heads/commit-4-8
	refs/heads/commit-9-9
	EOF
	cat >expect <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-5-3
	EOF
	run_all_modes git for-each-ref --merged=commit-9-6 \
		--format="%(refname)" --stdin
'

test_expect_success 'for-each-ref merged:some, multibase' '
	cat >input <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-5-3
	refs/heads/commit-7-8
	refs/heads/commit-4-8
	refs/heads/commit-9-9
	EOF
	cat >expect <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-4-8
	refs/heads/commit-5-3
	EOF
	run_all_modes git for-each-ref \
		--merged=commit-5-8 \
		--merged=commit-8-5 \
		--format="%(refname)" \
		--stdin
'

test_expect_success 'for-each-ref merged:none' '
	cat >input <<-\EOF &&
	refs/heads/commit-7-5
	refs/heads/commit-4-8
	refs/heads/commit-9-9
	EOF
	>expect &&
	run_all_modes git for-each-ref --merged=commit-8-4 \
		--format="%(refname)" --stdin
'

# For get_branch_base_for_tip, we only care about
# first-parent history. Here is the test graph with
# second parents removed:
#
#             (10,10)
#            /
#         (10,9)    (9,10)
#        /         /
#    (10,8)    (9,9)      (8,10)
#   /         /          /
#         ( continued...)
#   \     /        /           /
#    (3,1)     (2,2)      (1,3)
#        \     /          /
#         (2,1)      (1,2)
#              \    /
#              (1,1)
#
# In short, for a commit (i,j), the first-parent history
# walks all commits (i, k) with k from j to 1, then the
# commits (l, 1) with l from i to 1.

test_expect_success 'get_branch_base_for_tip: none reach' '
	# (2,3) branched from the first tip (i,4) in X with i > 2
	cat >input <<-\EOF &&
		A:commit-2-3
		X:commit-1-2
		X:commit-1-4
		X:commit-4-4
		X:commit-8-4
		X:commit-10-4
	EOF
	echo "get_branch_base_for_tip(A,X):2" >expect &&
	test_all_modes get_branch_base_for_tip
'

test_expect_success 'get_branch_base_for_tip: equal to tip' '
	# (2,3) branched from the first tip (i,4) in X with i > 2
	cat >input <<-\EOF &&
		A:commit-8-4
		X:commit-1-2
		X:commit-1-4
		X:commit-4-4
		X:commit-8-4
		X:commit-10-4
	EOF
	echo "get_branch_base_for_tip(A,X):3" >expect &&
	test_all_modes get_branch_base_for_tip
'

test_expect_success 'get_branch_base_for_tip: all reach tip' '
	# (2,3) branched from the first tip (i,4) in X with i > 2
	cat >input <<-\EOF &&
		A:commit-4-1
		X:commit-4-2
		X:commit-5-1
	EOF
	echo "get_branch_base_for_tip(A,X):0" >expect &&
	test_all_modes get_branch_base_for_tip
'

test_expect_success 'for-each-ref is-base: none reach' '
	cat >input <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-4-2
	refs/heads/commit-4-4
	refs/heads/commit-8-4
	EOF
	cat >expect <<-\EOF &&
	refs/heads/commit-1-1:
	refs/heads/commit-4-2:(commit-2-3)
	refs/heads/commit-4-4:
	refs/heads/commit-8-4:
	EOF
	run_all_modes git for-each-ref \
		--format="%(refname):%(is-base:commit-2-3)" --stdin
'

test_expect_success 'for-each-ref is-base: all reach' '
	cat >input <<-\EOF &&
	refs/heads/commit-4-2
	refs/heads/commit-5-1
	EOF
	cat >expect <<-\EOF &&
	refs/heads/commit-4-2:(commit-4-1)
	refs/heads/commit-5-1:
	EOF
	run_all_modes git for-each-ref \
		--format="%(refname):%(is-base:commit-4-1)" --stdin
'

test_expect_success 'for-each-ref is-base: equal to tip' '
	cat >input <<-\EOF &&
	refs/heads/commit-4-2
	refs/heads/commit-5-1
	EOF
	cat >expect <<-\EOF &&
	refs/heads/commit-4-2:(commit-4-2)
	refs/heads/commit-5-1:
	EOF
	run_all_modes git for-each-ref \
		--format="%(refname):%(is-base:commit-4-2)" --stdin
'

test_expect_success 'for-each-ref is-base:multiple' '
	cat >input <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-4-2
	refs/heads/commit-4-4
	refs/heads/commit-8-4
	EOF
	cat >expect <<-\EOF &&
	refs/heads/commit-1-1[-]
	refs/heads/commit-4-2[(commit-2-3)-]
	refs/heads/commit-4-4[-]
	refs/heads/commit-8-4[-(commit-6-5)]
	EOF
	run_all_modes git for-each-ref \
		--format="%(refname)[%(is-base:commit-2-3)-%(is-base:commit-6-5)]" --stdin
'

test_done
