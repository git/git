#!/bin/sh

test_description='basic cummit reachability tests'

. ./test-lib.sh

# Construct a grid-like cummit graph with points (x,y)
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
# We use branch 'cummit-x-y' to refer to (x,y).
# This grid allows interesting reachability and
# non-reachability queries: (x,y) can reach (x',y')
# if and only if x' <= x and y' <= y.
test_expect_success 'setup' '
	for i in $(test_seq 1 10)
	do
		test_cummit "1-$i" &&
		git branch -f cummit-1-$i &&
		git tag -a -m "1-$i" tag-1-$i cummit-1-$i || return 1
	done &&
	for j in $(test_seq 1 9)
	do
		git reset --hard cummit-$j-1 &&
		x=$(($j + 1)) &&
		test_cummit "$x-1" &&
		git branch -f cummit-$x-1 &&
		git tag -a -m "$x-1" tag-$x-1 cummit-$x-1 &&

		for i in $(test_seq 2 10)
		do
			git merge cummit-$j-$i -m "$x-$i" &&
			git branch -f cummit-$x-$i &&
			git tag -a -m "$x-$i" tag-$x-$i cummit-$x-$i || return 1
		done
	done &&
	git cummit-graph write --reachable &&
	mv .git/objects/info/cummit-graph cummit-graph-full &&
	chmod u+w cummit-graph-full &&
	git show-ref -s cummit-5-5 | git cummit-graph write --stdin-cummits &&
	mv .git/objects/info/cummit-graph cummit-graph-half &&
	chmod u+w cummit-graph-half &&
	git -c cummitGraph.generationVersion=1 cummit-graph write --reachable &&
	mv .git/objects/info/cummit-graph cummit-graph-no-gdat &&
	chmod u+w cummit-graph-no-gdat &&
	git config core.cummitGraph true
'

run_all_modes () {
	test_when_finished rm -rf .git/objects/info/cummit-graph &&
	"$@" <input >actual &&
	test_cmp expect actual &&
	cp cummit-graph-full .git/objects/info/cummit-graph &&
	"$@" <input >actual &&
	test_cmp expect actual &&
	cp cummit-graph-half .git/objects/info/cummit-graph &&
	"$@" <input >actual &&
	test_cmp expect actual &&
	cp cummit-graph-no-gdat .git/objects/info/cummit-graph &&
	"$@" <input >actual &&
	test_cmp expect actual
}

test_all_modes () {
	run_all_modes test-tool reach "$@"
}

test_expect_success 'ref_newer:miss' '
	cat >input <<-\EOF &&
	A:cummit-5-7
	B:cummit-4-9
	EOF
	echo "ref_newer(A,B):0" >expect &&
	test_all_modes ref_newer
'

test_expect_success 'ref_newer:hit' '
	cat >input <<-\EOF &&
	A:cummit-5-7
	B:cummit-2-3
	EOF
	echo "ref_newer(A,B):1" >expect &&
	test_all_modes ref_newer
'

test_expect_success 'in_merge_bases:hit' '
	cat >input <<-\EOF &&
	A:cummit-5-7
	B:cummit-8-8
	EOF
	echo "in_merge_bases(A,B):1" >expect &&
	test_all_modes in_merge_bases
'

test_expect_success 'in_merge_bases:miss' '
	cat >input <<-\EOF &&
	A:cummit-6-8
	B:cummit-5-9
	EOF
	echo "in_merge_bases(A,B):0" >expect &&
	test_all_modes in_merge_bases
'

test_expect_success 'in_merge_bases_many:hit' '
	cat >input <<-\EOF &&
	A:cummit-6-8
	X:cummit-6-9
	X:cummit-5-7
	EOF
	echo "in_merge_bases_many(A,X):1" >expect &&
	test_all_modes in_merge_bases_many
'

test_expect_success 'in_merge_bases_many:miss' '
	cat >input <<-\EOF &&
	A:cummit-6-8
	X:cummit-7-7
	X:cummit-8-6
	EOF
	echo "in_merge_bases_many(A,X):0" >expect &&
	test_all_modes in_merge_bases_many
'

test_expect_success 'in_merge_bases_many:miss-heuristic' '
	cat >input <<-\EOF &&
	A:cummit-6-8
	X:cummit-7-5
	X:cummit-6-6
	EOF
	echo "in_merge_bases_many(A,X):0" >expect &&
	test_all_modes in_merge_bases_many
'

test_expect_success 'is_descendant_of:hit' '
	cat >input <<-\EOF &&
	A:cummit-5-7
	X:cummit-4-8
	X:cummit-6-6
	X:cummit-1-1
	EOF
	echo "is_descendant_of(A,X):1" >expect &&
	test_all_modes is_descendant_of
'

test_expect_success 'is_descendant_of:miss' '
	cat >input <<-\EOF &&
	A:cummit-6-8
	X:cummit-5-9
	X:cummit-4-10
	X:cummit-7-6
	EOF
	echo "is_descendant_of(A,X):0" >expect &&
	test_all_modes is_descendant_of
'

test_expect_success 'get_merge_bases_many' '
	cat >input <<-\EOF &&
	A:cummit-5-7
	X:cummit-4-8
	X:cummit-6-6
	X:cummit-8-3
	EOF
	{
		echo "get_merge_bases_many(A,X):" &&
		git rev-parse cummit-5-6 \
			      cummit-4-7 | sort
	} >expect &&
	test_all_modes get_merge_bases_many
'

test_expect_success 'reduce_heads' '
	cat >input <<-\EOF &&
	X:cummit-1-10
	X:cummit-2-8
	X:cummit-3-6
	X:cummit-4-4
	X:cummit-1-7
	X:cummit-2-5
	X:cummit-3-3
	X:cummit-5-1
	EOF
	{
		echo "reduce_heads(X):" &&
		git rev-parse cummit-5-1 \
			      cummit-4-4 \
			      cummit-3-6 \
			      cummit-2-8 \
			      cummit-1-10 | sort
	} >expect &&
	test_all_modes reduce_heads
'

test_expect_success 'can_all_from_reach:hit' '
	cat >input <<-\EOF &&
	X:cummit-2-10
	X:cummit-3-9
	X:cummit-4-8
	X:cummit-5-7
	X:cummit-6-6
	X:cummit-7-5
	X:cummit-8-4
	X:cummit-9-3
	Y:cummit-1-9
	Y:cummit-2-8
	Y:cummit-3-7
	Y:cummit-4-6
	Y:cummit-5-5
	Y:cummit-6-4
	Y:cummit-7-3
	Y:cummit-8-1
	EOF
	echo "can_all_from_reach(X,Y):1" >expect &&
	test_all_modes can_all_from_reach
'

test_expect_success 'can_all_from_reach:miss' '
	cat >input <<-\EOF &&
	X:cummit-2-10
	X:cummit-3-9
	X:cummit-4-8
	X:cummit-5-7
	X:cummit-6-6
	X:cummit-7-5
	X:cummit-8-4
	X:cummit-9-3
	Y:cummit-1-9
	Y:cummit-2-8
	Y:cummit-3-7
	Y:cummit-4-6
	Y:cummit-5-5
	Y:cummit-6-4
	Y:cummit-8-5
	EOF
	echo "can_all_from_reach(X,Y):0" >expect &&
	test_all_modes can_all_from_reach
'

test_expect_success 'can_all_from_reach_with_flag: tags case' '
	cat >input <<-\EOF &&
	X:tag-2-10
	X:tag-3-9
	X:tag-4-8
	X:cummit-5-7
	X:cummit-6-6
	X:cummit-7-5
	X:cummit-8-4
	X:cummit-9-3
	Y:tag-1-9
	Y:tag-2-8
	Y:tag-3-7
	Y:cummit-4-6
	Y:cummit-5-5
	Y:cummit-6-4
	Y:cummit-7-3
	Y:cummit-8-1
	EOF
	echo "can_all_from_reach_with_flag(X,_,_,0,0):1" >expect &&
	test_all_modes can_all_from_reach_with_flag
'

test_expect_success 'cummit_contains:hit' '
	cat >input <<-\EOF &&
	A:cummit-7-7
	X:cummit-2-10
	X:cummit-3-9
	X:cummit-4-8
	X:cummit-5-7
	X:cummit-6-6
	X:cummit-7-5
	X:cummit-8-4
	X:cummit-9-3
	EOF
	echo "cummit_contains(_,A,X,_):1" >expect &&
	test_all_modes cummit_contains &&
	test_all_modes cummit_contains --tag
'

test_expect_success 'cummit_contains:miss' '
	cat >input <<-\EOF &&
	A:cummit-6-5
	X:cummit-2-10
	X:cummit-3-9
	X:cummit-4-8
	X:cummit-5-7
	X:cummit-6-6
	X:cummit-7-5
	X:cummit-8-4
	X:cummit-9-3
	EOF
	echo "cummit_contains(_,A,X,_):0" >expect &&
	test_all_modes cummit_contains &&
	test_all_modes cummit_contains --tag
'

test_expect_success 'rev-list: basic topo-order' '
	git rev-parse \
		cummit-6-6 cummit-5-6 cummit-4-6 cummit-3-6 cummit-2-6 cummit-1-6 \
		cummit-6-5 cummit-5-5 cummit-4-5 cummit-3-5 cummit-2-5 cummit-1-5 \
		cummit-6-4 cummit-5-4 cummit-4-4 cummit-3-4 cummit-2-4 cummit-1-4 \
		cummit-6-3 cummit-5-3 cummit-4-3 cummit-3-3 cummit-2-3 cummit-1-3 \
		cummit-6-2 cummit-5-2 cummit-4-2 cummit-3-2 cummit-2-2 cummit-1-2 \
		cummit-6-1 cummit-5-1 cummit-4-1 cummit-3-1 cummit-2-1 cummit-1-1 \
	>expect &&
	run_all_modes git rev-list --topo-order cummit-6-6
'

test_expect_success 'rev-list: first-parent topo-order' '
	git rev-parse \
		cummit-6-6 \
		cummit-6-5 \
		cummit-6-4 \
		cummit-6-3 \
		cummit-6-2 \
		cummit-6-1 cummit-5-1 cummit-4-1 cummit-3-1 cummit-2-1 cummit-1-1 \
	>expect &&
	run_all_modes git rev-list --first-parent --topo-order cummit-6-6
'

test_expect_success 'rev-list: range topo-order' '
	git rev-parse \
		cummit-6-6 cummit-5-6 cummit-4-6 cummit-3-6 cummit-2-6 cummit-1-6 \
		cummit-6-5 cummit-5-5 cummit-4-5 cummit-3-5 cummit-2-5 cummit-1-5 \
		cummit-6-4 cummit-5-4 cummit-4-4 cummit-3-4 cummit-2-4 cummit-1-4 \
		cummit-6-3 cummit-5-3 cummit-4-3 \
		cummit-6-2 cummit-5-2 cummit-4-2 \
		cummit-6-1 cummit-5-1 cummit-4-1 \
	>expect &&
	run_all_modes git rev-list --topo-order cummit-3-3..cummit-6-6
'

test_expect_success 'rev-list: range topo-order' '
	git rev-parse \
		cummit-6-6 cummit-5-6 cummit-4-6 \
		cummit-6-5 cummit-5-5 cummit-4-5 \
		cummit-6-4 cummit-5-4 cummit-4-4 \
		cummit-6-3 cummit-5-3 cummit-4-3 \
		cummit-6-2 cummit-5-2 cummit-4-2 \
		cummit-6-1 cummit-5-1 cummit-4-1 \
	>expect &&
	run_all_modes git rev-list --topo-order cummit-3-8..cummit-6-6
'

test_expect_success 'rev-list: first-parent range topo-order' '
	git rev-parse \
		cummit-6-6 \
		cummit-6-5 \
		cummit-6-4 \
		cummit-6-3 \
		cummit-6-2 \
		cummit-6-1 cummit-5-1 cummit-4-1 \
	>expect &&
	run_all_modes git rev-list --first-parent --topo-order cummit-3-8..cummit-6-6
'

test_expect_success 'rev-list: ancestry-path topo-order' '
	git rev-parse \
		cummit-6-6 cummit-5-6 cummit-4-6 cummit-3-6 \
		cummit-6-5 cummit-5-5 cummit-4-5 cummit-3-5 \
		cummit-6-4 cummit-5-4 cummit-4-4 cummit-3-4 \
		cummit-6-3 cummit-5-3 cummit-4-3 \
	>expect &&
	run_all_modes git rev-list --topo-order --ancestry-path cummit-3-3..cummit-6-6
'

test_expect_success 'rev-list: symmetric difference topo-order' '
	git rev-parse \
		cummit-6-6 cummit-5-6 cummit-4-6 \
		cummit-6-5 cummit-5-5 cummit-4-5 \
		cummit-6-4 cummit-5-4 cummit-4-4 \
		cummit-6-3 cummit-5-3 cummit-4-3 \
		cummit-6-2 cummit-5-2 cummit-4-2 \
		cummit-6-1 cummit-5-1 cummit-4-1 \
		cummit-3-8 cummit-2-8 cummit-1-8 \
		cummit-3-7 cummit-2-7 cummit-1-7 \
	>expect &&
	run_all_modes git rev-list --topo-order cummit-3-8...cummit-6-6
'

test_expect_success 'get_reachable_subset:all' '
	cat >input <<-\EOF &&
	X:cummit-9-1
	X:cummit-8-3
	X:cummit-7-5
	X:cummit-6-6
	X:cummit-1-7
	Y:cummit-3-3
	Y:cummit-1-7
	Y:cummit-5-6
	EOF
	(
		echo "get_reachable_subset(X,Y)" &&
		git rev-parse cummit-3-3 \
			      cummit-1-7 \
			      cummit-5-6 | sort
	) >expect &&
	test_all_modes get_reachable_subset
'

test_expect_success 'get_reachable_subset:some' '
	cat >input <<-\EOF &&
	X:cummit-9-1
	X:cummit-8-3
	X:cummit-7-5
	X:cummit-1-7
	Y:cummit-3-3
	Y:cummit-1-7
	Y:cummit-5-6
	EOF
	(
		echo "get_reachable_subset(X,Y)" &&
		git rev-parse cummit-3-3 \
			      cummit-1-7 | sort
	) >expect &&
	test_all_modes get_reachable_subset
'

test_expect_success 'get_reachable_subset:none' '
	cat >input <<-\EOF &&
	X:cummit-9-1
	X:cummit-8-3
	X:cummit-7-5
	X:cummit-1-7
	Y:cummit-9-3
	Y:cummit-7-6
	Y:cummit-2-8
	EOF
	echo "get_reachable_subset(X,Y)" >expect &&
	test_all_modes get_reachable_subset
'

test_done
