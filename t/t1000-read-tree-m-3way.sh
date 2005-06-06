#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Three way merge with read-tree -m

This test tries three-way merge with read-tree -m

There is one ancestor (called O for Original) and two branches A
and B derived from it.  We want to do a 3-way merge between A and
B, using O as the common ancestor.

    merge A O B

Decisions are made by comparing contents of O, A and B pathname
by pathname.  The result is determined by the following guiding
principle:

 - If only A does something to it and B does not touch it, take
   whatever A does.

 - If only B does something to it and A does not touch it, take
   whatever B does.

 - If both A and B does something but in the same way, take
   whatever they do.

 - If A and B does something but different things, we need a
   3-way merge:

   - We cannot do anything about the following cases:

     * O does not have it.  A and B both must be adding to the
       same path independently.

     * A deletes it.  B must be modifying.

   - Otherwise, A and B are modifying.  Run 3-way merge.

First, the case matrix.

 - Vertical axis is for A'\''s actions.
 - Horizontal axis is for B'\''s actions.

.----------------------------------------------------------------.
| A        B | No Action  |   Delete   |   Modify   |    Add     |
|------------+------------+------------+------------+------------|
| No Action  |            |            |            |            |
|            | select O   | delete     | select B   | select B   |
|            |            |            |            |            |
|------------+------------+------------+------------+------------|
| Delete     |            |            | ********** |    can     |
|            | delete     | delete     | merge      |    not     |
|            |            |            |            |  happen    |
|------------+------------+------------+------------+------------|
| Modify     |            | ********** | ?????????? |    can     |
|            | select A   | merge      | select A=B |    not     |
|            |            |            | merge      |  happen    |
|------------+------------+------------+------------+------------|
| Add        |            |    can     |    can     | ?????????? |
|            | select A   |    not     |    not     | select A=B |
|            |            |  happen    |  happen    | merge      |
.----------------------------------------------------------------.

In addition:

 SS: a special case of MM, where A and B makes the same modification.
 LL: a special case of AA, where A and B creates the same file.
 TT: a special case of MM, where A and B makes mergeable changes.
 DF: a special case, where A makes a directory and B makes a file.

'
. ./test-lib.sh
. ../lib-read-tree-m-3way.sh

################################################################
# Try merging and showing the various diffs

# The tree is dirty at this point.
test_expect_failure \
    '3-way merge with git-read-tree -m' \
    "git-read-tree -m $tree_O $tree_A $tree_B"

# This is done on an empty work directory, which is the normal
# merge person behaviour.
test_expect_success \
    '3-way merge with git-read-tree -m' \
    "rm -fr [NDMALTS][NDMALTSF] Z &&
     rm .git/index &&
     git-read-tree -m $tree_O $tree_A $tree_B"

# This starts out with the first head, which is the normal
# patch submitter behaviour.
test_expect_success \
    '3-way merge with git-read-tree -m' \
    "git-read-tree $tree_A &&
     git-checkout-cache -f -u -a &&
     git-read-tree -m $tree_O $tree_A $tree_B"

_x40='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x40="$_x40$_x40$_x40$_x40$_x40$_x40$_x40$_x40"
test_expect_success \
    'git-ls-files --stage of the merge result' \
    'git-ls-files --stage >current- &&
     sed -e "s/ $_x40 / X /" <current- >current'

cat >expected <<\EOF
100644 X 2	AA
100644 X 3	AA
100644 X 2	AN
100644 X 1	DD
100644 X 3	DF
100644 X 2	DF/DF
100644 X 1	DM
100644 X 3	DM
100644 X 1	DN
100644 X 3	DN
100644 X 2	LL
100644 X 3	LL
100644 X 1	MD
100644 X 2	MD
100644 X 1	MM
100644 X 2	MM
100644 X 3	MM
100644 X 0	MN
100644 X 3	NA
100644 X 1	ND
100644 X 2	ND
100644 X 0	NM
100644 X 0	NN
100644 X 0	SS
100644 X 1	TT
100644 X 2	TT
100644 X 3	TT
100644 X 2	Z/AA
100644 X 3	Z/AA
100644 X 2	Z/AN
100644 X 1	Z/DD
100644 X 1	Z/DM
100644 X 3	Z/DM
100644 X 1	Z/DN
100644 X 3	Z/DN
100644 X 1	Z/MD
100644 X 2	Z/MD
100644 X 1	Z/MM
100644 X 2	Z/MM
100644 X 3	Z/MM
100644 X 0	Z/MN
100644 X 3	Z/NA
100644 X 1	Z/ND
100644 X 2	Z/ND
100644 X 0	Z/NM
100644 X 0	Z/NN
EOF

test_expect_success \
    'validate merge result' \
    'diff current expected'

test_done
