#!/bin/sh

test_description='test handling of inter-pack delta cycles during repack

The goal here is to create a situation where we have two blobs, A and B, with A
as a delta against B in one pack, and vice versa in the other. Then if we can
persuade a full repack to find A from one pack and B from the other, that will
give us a cycle when we attempt to reuse those deltas.

The trick is in the "persuade" step, as it depends on the internals of how
pack-objects picks which pack to reuse the deltas from. But we can assume
that it does so in one of two general strategies:

 1. Using a static ordering of packs. In this case, no inter-pack cycles can
    happen. Any objects with a delta relationship must be present in the same
    pack (i.e., no "--thin" packs on disk), so we will find all related objects
    from that pack. So assuming there are no cycles within a single pack (and
    we avoid generating them via pack-objects or importing them via
    index-pack), then our result will have no cycles.

    So this case should pass the tests no matter how we arrange things.

 2. Picking the next pack to examine based on locality (i.e., where we found
    something else recently).

    In this case, we want to make sure that we find the delta versions of A and
    B and not their base versions. We can do this by putting two blobs in each
    pack. The first is a "dummy" blob that can only be found in the pack in
    question.  And then the second is the actual delta we want to find.

    The two blobs must be present in the same tree, not present in other trees,
    and the dummy pathname must sort before the delta path.

The setup below focuses on case 2. We have two commits HEAD and HEAD^, each
which has two files: "dummy" and "file". Then we can make two packs which
contain:

  [pack one]
  HEAD:dummy
  HEAD:file  (as delta against HEAD^:file)
  HEAD^:file (as base)

  [pack two]
  HEAD^:dummy
  HEAD^:file (as delta against HEAD:file)
  HEAD:file  (as base)

Then no matter which order we start looking at the packs in, we know that we
will always find a delta for "file", because its lookup will always come
immediately after the lookup for "dummy".
'
. ./test-lib.sh



# Create a pack containing the the tree $1 and blob $1:file, with
# the latter stored as a delta against $2:file.
#
# We convince pack-objects to make the delta in the direction of our choosing
# by marking $2 as a preferred-base edge. That results in $1:file as a thin
# delta, and index-pack completes it by adding $2:file as a base.
#
# Note that the two variants of "file" must be similar enough to convince git
# to create the delta.
make_pack () {
	{
		printf '%s\n' "-$(git rev-parse $2)"
		printf '%s dummy\n' "$(git rev-parse $1:dummy)"
		printf '%s file\n' "$(git rev-parse $1:file)"
	} |
	git pack-objects --stdout |
	git index-pack --stdin --fix-thin
}

test_expect_success 'setup' '
	test-tool genrandom base 4096 >base &&
	for i in one two
	do
		# we want shared content here to encourage deltas...
		cp base file &&
		echo $i >>file &&

		# ...whereas dummy should be short, because we do not want
		# deltas that would create duplicates when we --fix-thin
		echo $i >dummy &&

		git add file dummy &&
		test_tick &&
		git commit -m $i ||
		return 1
	done &&

	make_pack HEAD^ HEAD &&
	make_pack HEAD HEAD^
'

test_expect_success 'repack' '
	# We first want to check that we do not have any internal errors,
	# and also that we do not hit the last-ditch cycle-breaking code
	# in write_object(), which will issue a warning to stderr.
	git repack -ad 2>stderr &&
	test_must_be_empty stderr &&

	# And then double-check that the resulting pack is usable (i.e.,
	# we did not fail to notice any cycles). We know we are accessing
	# the objects via the new pack here, because "repack -d" will have
	# removed the others.
	git cat-file blob HEAD:file >/dev/null &&
	git cat-file blob HEAD^:file >/dev/null
'

test_done
