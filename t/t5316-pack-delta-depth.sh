#!/bin/sh

test_description='pack-objects breaks long cross-pack delta chains'
. ./test-lib.sh

# This mirrors a repeated push setup:
#
# 1. A client repeatedly modifies some files, makes a
#      commit, and pushes the result. It does this N times
#      before we get around to repacking.
#
# 2. Each push generates a thin pack with the new version of
#    various objects. Let's consider some file in the root tree
#    which is updated in each commit.
#
#    When generating push number X, we feed commit X-1 (and
#    thus blob X-1) as a preferred base. The resulting pack has
#    blob X as a thin delta against blob X-1.
#
#    On the receiving end, "index-pack --fix-thin" will
#    complete the pack with a base copy of blob X-1.
#
# 3. In older versions of git, if we used the delta from
#    pack X, then we'd always find blob X-1 as a base in the
#    same pack (and generate a fresh delta).
#
#    But with the pack mru, we jump from delta to delta
#    following the traversal order:
#
#      a. We grab blob X from pack X as a delta, putting it at
#         the tip of our mru list.
#
#      b. Eventually we move onto commit X-1. We need other
#         objects which are only in pack X-1 (in the test code
#         below, it's the containing tree). That puts pack X-1
#         at the tip of our mru list.
#
#      c. Eventually we look for blob X-1, and we find the
#         version in pack X-1 (because it's the mru tip).
#
# Now we have blob X as a delta against X-1, which is a delta
# against X-2, and so forth.
#
# In the real world, these small pushes would get exploded by
# unpack-objects rather than "index-pack --fix-thin", but the
# same principle applies to larger pushes (they only need one
# repeatedly-modified file to generate the delta chain).

test_expect_success 'create series of packs' '
	test-genrandom foo 4096 >content &&
	prev= &&
	for i in $(test_seq 1 10)
	do
		cat content >file &&
		echo $i >>file &&
		git add file &&
		git commit -m $i &&
		cur=$(git rev-parse HEAD^{tree}) &&
		{
			test -n "$prev" && echo "-$prev"
			echo $cur
			echo "$(git rev-parse :file) file"
		} | git pack-objects --stdout >tmp &&
		git index-pack --stdin --fix-thin <tmp || return 1
		prev=$cur
	done
'

max_chain() {
	git index-pack --verify-stat-only "$1" >output &&
	perl -lne '
	  /chain length = (\d+)/ and $len = $1;
	  END { print $len }
	' output
}

# Note that this whole setup is pretty reliant on the current
# packing heuristics. We double-check that our test case
# actually produces a long chain. If it doesn't, it should be
# adjusted (or scrapped if the heuristics have become too unreliable)
test_expect_success 'packing produces a long delta' '
	# Use --window=0 to make sure we are seeing reused deltas,
	# not computing a new long chain.
	pack=$(git pack-objects --all --window=0 </dev/null pack) &&
	test 9 = "$(max_chain pack-$pack.pack)"
'

test_expect_success '--depth limits depth' '
	pack=$(git pack-objects --all --depth=5 </dev/null pack) &&
	test 5 = "$(max_chain pack-$pack.pack)"
'

test_done
