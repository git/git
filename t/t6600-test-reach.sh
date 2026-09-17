#!/bin/sh

test_description='basic commit reachability tests'

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
	# Build a topology with clock skew to test the !FIND_ALL early
	# exit in paint_down_to_common().  M2 is the correct merge base
	# of P1 and P2, but its ancestor M1 has a higher committer date
	# due to clock skew.  With date-only ordering (v1 commit graph
	# without corrected commit dates), M1 pops from the queue first,
	# gets both paint sides, and the early exit fires before M2 is
	# ever visited.
	#
	#        P1     P2          @7000
	#        |     /  \
	#        A    B    D        @6000
	#       / \   |    |
	#      |  M2--+    |        @2000 (correct merge base)
	#       \ |        |
	#        M1--------+        @5000 (clock skew: date > M2)
	#        |
	#       root                @1000
	#
	git checkout --orphan skew-orphan &&
	skew_tree=$(git mktree </dev/null) &&
	skew_commit () {
		GIT_COMMITTER_DATE="@$1 +0000" GIT_AUTHOR_DATE="@$1 +0000" \
			git commit-tree -m "$2" "$skew_tree" $3 $4 $5 $6
	} &&
	skew_root=$(skew_commit 1000 root) &&
	skew_M1=$(skew_commit 5000 M1 -p "$skew_root") &&
	skew_M2=$(skew_commit 2000 M2 -p "$skew_M1") &&
	skew_A=$(skew_commit 6000 A -p "$skew_M1" -p "$skew_M2") &&
	skew_B=$(skew_commit 6000 B -p "$skew_M2") &&
	skew_D=$(skew_commit 6000 D -p "$skew_M1") &&
	skew_P1=$(skew_commit 7000 P1 -p "$skew_A") &&
	skew_P2=$(skew_commit 7000 P2 -p "$skew_B" -p "$skew_D") &&
	git branch -f skew-P1 "$skew_P1" &&
	git branch -f skew-P2 "$skew_P2" &&
	git tag skew-M2 "$skew_M2" &&

	# Build a small side topology to exercise the (PARENT1|PARENT2) ->
	# (PARENT1|PARENT2|STALE) transition in paint_down_to_common(); the
	# 10x10 grid above does not exercise it because no merge-base candidate
	# there is a descendant of another, so STALE never reaches a
	# still-pending candidate.
	#
	#       ps-X
	#       /|\
	#      / | \
	#   ps-Z ps-B ps-W
	#     |  / \  |
	#     | /   \ |
	#     |/     \|
	#   ps-T1   ps-T2
	#
	# where ps-T1=merge(ps-Z,ps-B), ps-T2=merge(ps-W,ps-B), so
	# merge-base(ps-T1,ps-T2) = ps-B.  During the walk, ps-X transitions
	# to (PARENT1|PARENT2) via ps-Z and ps-W before ps-B is dequeued;
	# then the STALE-walk from ps-B transitions ps-X to
	# (PARENT1|PARENT2|STALE).
	git checkout --orphan ps-orphan &&
	test_commit ps-X &&
	git checkout -b ps-B-br ps-X && test_commit ps-B &&
	git checkout -b ps-Z-br ps-X && test_commit ps-Z &&
	git checkout -b ps-W-br ps-X && test_commit ps-W &&
	git checkout -b ps-T1 ps-Z &&
	git merge --no-ff -m ps-T1 ps-B &&
	git checkout -b ps-T2 ps-W &&
	git merge --no-ff -m ps-T2 ps-B &&

	# Build a side topology that lives entirely outside the half
	# commit-graph and has non-monotonic commit dates, to exercise the
	# INFINITY-gate in paint_down_to_common.  With both tips outside
	# the graph, generation is INFINITY and the queue falls back to
	# commit-date order, which here is non-monotonic.
	#
	#   pi-X (date 500, PARENT1 tip) --> pi-P, pi-D
	#   pi-D (date 480) --> pi-C
	#   pi-C (date 200) --> pi-B
	#   pi-B (date 100, PARENT2 tip) --> pi-P
	#   pi-P (date 450, root)
	#
	# merge-base(pi-X, pi-B) = pi-B (it is an ancestor of pi-X and is
	# itself one of the queried tips).
	git checkout --orphan pi-orphan &&
	test_commit --date "@450 +0000" pi-P &&
	test_commit --date "@100 +0000" pi-B &&
	test_commit --date "@200 +0000" pi-C &&
	test_commit --date "@480 +0000" pi-D &&
	GIT_AUTHOR_DATE="@500 +0000" GIT_COMMITTER_DATE="@500 +0000" \
		git commit-tree -p pi-D -p pi-P -m pi-X pi-D^{tree} >pi-X-oid &&
	pi_x="$(cat pi-X-oid)" &&
	git branch -f pi-X-br "$pi_x" &&
	git tag pi-X "$pi_x" &&

	# Clock-skew topology for side-exhaustion testing.
	# D is the correct merge base but has a higher committer date
	# than C (its child).  With date ordering, D would be dequeued
	# before C, causing side-exhaustion to fire too early.
	# Generation ordering prevents this by visiting children
	# before parents regardless of dates.
	#
	#   se-A (date 7000) --> se-C (date 3000) --> se-D (date 5000) --> se-root (date 4000)
	#   se-B (date 6000) --> se-D
	#
	se_root=$(skew_commit 4000 se-root) &&
	se_D=$(skew_commit 5000 se-D -p "$se_root") &&
	se_C=$(skew_commit 3000 se-C -p "$se_D") &&
	se_A=$(skew_commit 7000 se-A -p "$se_C") &&
	se_B=$(skew_commit 6000 se-B -p "$se_D") &&
	git branch -f se-A "$se_A" &&
	git branch -f se-B "$se_B" &&
	git tag se-D "$se_D" &&

	# Clock-skew topology with redundant ancestor for
	# side-exhaustion testing.  MB1 is the correct merge base;
	# MB2 is its parent.  A reaches MB2 via E (high date) and
	# MB1 via C (low date).  B reaches MB1 via D.  With date
	# ordering, side-exhaustion would fire before C is dequeued,
	# missing MB1.  Generation ordering ensures both are found.
	#
	#   se2-A (date 8000) --> se2-C (date 2000) --> se2-MB1 (date 5000) --> se2-MB2 (date 4000) --> se2-root (date 1000)
	#   se2-A              --> se2-E (date 6500) --> se2-MB2
	#   se2-B (date 7000) --> se2-D (date 6000) --> se2-MB1
	#
	se2_root=$(skew_commit 1000 se2-root) &&
	se2_MB2=$(skew_commit 4000 se2-MB2 -p "$se2_root") &&
	se2_MB1=$(skew_commit 5000 se2-MB1 -p "$se2_MB2") &&
	se2_C=$(skew_commit 2000 se2-C -p "$se2_MB1") &&
	se2_D=$(skew_commit 6000 se2-D -p "$se2_MB1") &&
	se2_E=$(skew_commit 6500 se2-E -p "$se2_MB2") &&
	se2_A=$(skew_commit 8000 se2-A -p "$se2_C" -p "$se2_E") &&
	se2_B=$(skew_commit 7000 se2-B -p "$se2_D") &&
	git branch -f se2-A "$se2_A" &&
	git branch -f se2-B "$se2_B" &&
	git tag se2-MB1 "$se2_MB1" &&

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
	graph=.git/objects/info/commit-graph &&
	test_when_finished rm -rf "$graph" "${graph}s" &&
	rm -f trace-mode-*.txt &&

	for mode in none full half no-gdat
	do
		rm -rf "$graph" "${graph}s" &&
		cp "commit-graph-${mode}" "$graph" 2>/dev/null ||
		true &&
		GIT_TRACE2_EVENT="$(pwd)/trace-mode-${mode}.txt" \
			"$@" <input >actual &&
		test_cmp expect actual || return 1
	done
}

test_all_modes () {
	run_all_modes test-tool reach "$@"
}

test_paint_down_steps () {
	for mode in none full half no-gdat
	do
		test_trace2_data_singular paint_down_to_common steps "$1" \
			"mode=$mode" <"trace-mode-${mode}.txt" || return 1
		shift
	done
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

test_expect_success 'in_merge_bases_many:self' '
	cat >input <<-\EOF &&
	A:commit-6-8
	X:commit-5-9
	X:commit-6-8
	EOF
	echo "in_merge_bases_many(A,X):1" >expect &&
	test_all_modes in_merge_bases_many &&
	test_paint_down_steps 45 1 25 1
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

test_expect_success 'get_merge_bases_many:duplicate-twos' '
	cat >input <<-\EOF &&
	A:commit-5-7
	X:commit-4-8
	X:commit-4-8
	X:commit-6-6
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

test_expect_success 'get_merge_bases_many:pending-stale' '
	# Exercises the (PARENT1|PARENT2) -> (...|STALE) transition path in
	# paint_down_to_common().  See the topology comment in the setup test.
	cat >input <<-\EOF &&
	A:ps-T1
	X:ps-T2
	EOF
	{
		echo "get_merge_bases_many(A,X):" &&
		git rev-parse ps-B
	} >expect &&
	test_all_modes get_merge_bases_many &&
	test_paint_down_steps 5 5 5 5
'

test_expect_success 'get_merge_bases_many:infinity-both-sides' '
	# Exercises the push-time INFINITY-gate in paint_down_to_common().  See
	# the pi-* topology comment in the setup test.
	cat >input <<-\EOF &&
	A:pi-X
	X:pi-B
	EOF
	{
		echo "get_merge_bases_many(A,X):" &&
		git rev-parse pi-B
	} >expect &&
	test_all_modes get_merge_bases_many &&
	test_paint_down_steps 5 4 5 4
'

test_expect_success 'setup mixed finite/INFINITY topology' '
	# Create a commit outside all saved commit-graph files so it always
	# has INFINITY generation, while its parent (ps-X) is in the graph
	# with a finite generation. Use the ps-* orphan topology so we do
	# not pollute the grid-based rev-list tests.
	git checkout ps-X &&
	test_env GIT_TEST_COMMIT_GRAPH= test_commit pm-INF
'

test_expect_success 'get_merge_bases_many:mixed-finite-infinity' '
	# One tip (pm-INF) is outside the commit-graph with INFINITY
	# generation; the other (ps-B) is in the graph with finite
	# generation. The walk starts in the INFINITY region and crosses
	# into the finite region where side-exhaustion can fire.
	cat >input <<-\EOF &&
	A:pm-INF
	X:ps-B
	EOF
	{
		echo "get_merge_bases_many(A,X):" &&
		git rev-parse ps-X
	} >expect &&
	test_all_modes get_merge_bases_many &&
	test_paint_down_steps 3 3 3 3
'

test_expect_success 'merge-base --all commit-walk steps' '
	>input &&
	git rev-parse commit-9-1 >expect &&
	run_all_modes git merge-base --all commit-9-9 commit-9-1 &&
	test_paint_down_steps 81 9 57 37
'

test_expect_success 'merge-base --all with clock skew (side-exhaustion)' '
	# Verify that the merge base is computed correctly even
	# when commits have non-monotonic commit dates.
	>input &&
	git rev-parse se-D >expect &&
	run_all_modes git merge-base --all se-A se-B &&
	test_paint_down_steps 6 4 6 4
'

test_expect_success 'merge-base --all with clock skew and redundant ancestor (side-exhaustion)' '
	# Verify that the correct merge base is found even when
	# non-monotonic commit dates could cause a redundant
	# ancestor to be visited first.
	>input &&
	git rev-parse se2-MB1 >expect &&
	run_all_modes git merge-base --all se2-A se2-B &&
	test_paint_down_steps 8 6 8 6
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

test_expect_success 'for-each-ref merged:duplicate, all reachable' '
	git branch dup-a commit-3-3 &&
	git branch dup-b commit-3-3 &&
	cat >input <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/dup-a
	refs/heads/dup-b
	EOF
	cat >expect <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/dup-a
	refs/heads/dup-b
	EOF
	run_all_modes git for-each-ref --merged=commit-5-5 \
		--format="%(refname)" --stdin
'

test_expect_success 'for-each-ref merged:duplicate, none reachable' '
	cat >input <<-\EOF &&
	refs/heads/dup-a
	refs/heads/dup-b
	refs/heads/commit-9-9
	EOF
	>expect &&
	run_all_modes git for-each-ref --merged=commit-2-2 \
		--format="%(refname)" --stdin
'

test_expect_success 'for-each-ref merged:duplicate at min generation' '
	git branch dup-c commit-1-1 &&
	git branch dup-d commit-1-1 &&
	cat >input <<-\EOF &&
	refs/heads/dup-c
	refs/heads/dup-d
	refs/heads/commit-5-5
	EOF
	cat >expect <<-\EOF &&
	refs/heads/commit-5-5
	refs/heads/dup-c
	refs/heads/dup-d
	EOF
	run_all_modes git for-each-ref --merged=commit-5-5 \
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

test_expect_success 'for-each-ref is-base: --sort' '
	cat >input <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-4-2
	refs/heads/commit-4-4
	refs/heads/commit-8-4
	EOF

	cat >expect <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-4-4
	refs/heads/commit-8-4
	refs/heads/commit-4-2
	EOF
	run_all_modes git for-each-ref \
		--format="%(refname)" --stdin \
		--sort=refname --sort=is-base:commit-2-3 &&

	cat >expect <<-\EOF &&
	refs/heads/commit-4-2
	refs/heads/commit-1-1
	refs/heads/commit-4-4
	refs/heads/commit-8-4
	EOF
	run_all_modes git for-each-ref \
		--format="%(refname)" --stdin \
		--sort=refname --sort=-is-base:commit-2-3
'

test_expect_success 'rev-list --maximal-only (all positive)' '
	# Only one maximal.
	cat >input <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-4-2
	refs/heads/commit-4-4
	refs/heads/commit-8-4
	EOF

	cat >expect <<-EOF &&
	$(git rev-parse refs/heads/commit-8-4)
	EOF
	run_all_modes git rev-list --maximal-only --stdin &&

	# All maximal.
	cat >input <<-\EOF &&
	refs/heads/commit-5-2
	refs/heads/commit-4-3
	refs/heads/commit-3-4
	refs/heads/commit-2-5
	EOF

	cat >expect <<-EOF &&
	$(git rev-parse refs/heads/commit-5-2)
	$(git rev-parse refs/heads/commit-4-3)
	$(git rev-parse refs/heads/commit-3-4)
	$(git rev-parse refs/heads/commit-2-5)
	EOF
	run_all_modes git rev-list --maximal-only --stdin &&

	# Mix of both.
	cat >input <<-\EOF &&
	refs/heads/commit-5-2
	refs/heads/commit-3-2
	refs/heads/commit-2-5
	EOF

	cat >expect <<-EOF &&
	$(git rev-parse refs/heads/commit-5-2)
	$(git rev-parse refs/heads/commit-2-5)
	EOF
	run_all_modes git rev-list --maximal-only --stdin
'

test_expect_success 'rev-list --maximal-only (range)' '
	cat >input <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-2-5
	refs/heads/commit-6-4
	^refs/heads/commit-4-5
	EOF

	cat >expect <<-EOF &&
	$(git rev-parse refs/heads/commit-6-4)
	EOF
	run_all_modes git rev-list --maximal-only --stdin &&

	# first-parent changes reachability: the first parent
	# reduces the second coordinate to 1 before reducing the
	# first coordinate.
	cat >input <<-\EOF &&
	refs/heads/commit-1-1
	refs/heads/commit-2-5
	refs/heads/commit-6-4
	^refs/heads/commit-4-5
	EOF

	cat >expect <<-EOF &&
	$(git rev-parse refs/heads/commit-6-4)
	$(git rev-parse refs/heads/commit-2-5)
	EOF
	run_all_modes git rev-list --maximal-only --stdin \
		--first-parent --exclude-first-parent-only
'

test_expect_success 'rev-list --maximal-only matches merge-base --independent' '
	# Mix of independent and dependent
	git merge-base --independent \
		refs/heads/commit-5-2 \
		refs/heads/commit-3-2 \
		refs/heads/commit-2-5 >expect &&
	sort expect >expect.sorted &&
	git rev-list --maximal-only \
		refs/heads/commit-5-2 \
		refs/heads/commit-3-2 \
		refs/heads/commit-2-5 >actual &&
	sort actual >actual.sorted &&
	test_cmp expect.sorted actual.sorted &&

	# All independent commits.
	git merge-base --independent \
		refs/heads/commit-5-2 \
		refs/heads/commit-4-3 \
		refs/heads/commit-3-4 \
		refs/heads/commit-2-5 >expect &&
	sort expect >expect.sorted &&
	git rev-list --maximal-only \
		refs/heads/commit-5-2 \
		refs/heads/commit-4-3 \
		refs/heads/commit-3-4 \
		refs/heads/commit-2-5 >actual &&
	sort actual >actual.sorted &&
	test_cmp expect.sorted actual.sorted &&

	# Only one independent.
	git merge-base --independent \
		refs/heads/commit-1-1 \
		refs/heads/commit-4-2 \
		refs/heads/commit-4-4 \
		refs/heads/commit-8-4 >expect &&
	sort expect >expect.sorted &&
	git rev-list --maximal-only \
		refs/heads/commit-1-1 \
		refs/heads/commit-4-2 \
		refs/heads/commit-4-4 \
		refs/heads/commit-8-4 >actual &&
	sort actual >actual.sorted &&
	test_cmp expect.sorted actual.sorted
'

# The following tests verify the early-exit optimisation in
# paint_down_to_common when merge-base is invoked without --all.
# Each test checks all four commit-graph configurations.

merge_base_all_modes () {
	test_when_finished rm -rf .git/objects/info/commit-graph &&
	git merge-base "$@" >actual &&
	test_cmp expect actual &&
	cp commit-graph-full .git/objects/info/commit-graph &&
	git merge-base "$@" >actual &&
	test_cmp expect actual &&
	cp commit-graph-half .git/objects/info/commit-graph &&
	git merge-base "$@" >actual &&
	test_cmp expect actual &&
	cp commit-graph-no-gdat .git/objects/info/commit-graph &&
	git merge-base "$@" >actual &&
	test_cmp expect actual
}

test_expect_success 'merge-base without --all (unique base)' '
	git rev-parse commit-5-3 >expect &&
	merge_base_all_modes commit-5-7 commit-8-3
'

test_expect_success 'merge-base without --all is one of --all results' '
	test_when_finished rm -rf .git/objects/info/commit-graph &&

	cp commit-graph-full .git/objects/info/commit-graph &&
	git merge-base --all commit-5-7 commit-4-8 commit-6-6 commit-8-3 >all &&
	git merge-base commit-5-7 commit-4-8 commit-6-6 commit-8-3 >single &&
	test_line_count = 1 single &&
	test_grep -F -f single all &&

	cp commit-graph-half .git/objects/info/commit-graph &&
	git merge-base --all commit-5-7 commit-4-8 commit-6-6 commit-8-3 >all &&
	git merge-base commit-5-7 commit-4-8 commit-6-6 commit-8-3 >single &&
	test_line_count = 1 single &&
	test_grep -F -f single all
'

test_expect_success 'merge-base without --all, clock skew, v1 commit-graph' '
	git rev-parse skew-M2 >expect &&
	merge_base_all_modes skew-P1 skew-P2
'

test_done
