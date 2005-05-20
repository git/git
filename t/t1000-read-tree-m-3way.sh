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

# Original tree.
mkdir Z
for a in N D M
do
    for b in N D M
    do
        p=$a$b
	echo This is $p from the original tree. >$p
	echo This is Z/$p from the original tree. >Z/$p
	test_expect_success \
	    "adding test file $p and Z/$p" \
	    'git-update-cache --add $p &&
	    git-update-cache --add Z/$p'
    done
done
echo This is SS from the original tree. >SS
test_expect_success \
    'adding test file SS' \
    'git-update-cache --add SS'
cat >TT <<\EOF
This is a trivial merge sample text.
Branch A is expected to upcase this word, here.
There are some filler lines to avoid diff context
conflicts here,
like this one,
and this one,
and this one is yet another one of them.
At the very end, here comes another line, that is
the word, expected to be upcased by Branch B.
This concludes the trivial merge sample file.
EOF
test_expect_success \
    'adding test file TT' \
    'git-update-cache --add TT'
test_expect_success \
    'prepare initial tree' \
    'tree_O=$(git-write-tree)'

test_expect_success \
    'commit initial tree' \
    'commit_O=$(echo "Original tree for the merge test." |
     git-commit-tree $tree_O)'
echo $commit_O >.git/HEAD-O

################################################################
# Branch A and B makes the changes according to the above matrix.

################################################################
# Branch A

to_remove=$(echo D? Z/D?)
rm -f $to_remove
test_expect_success \
    'change in branch A (removal)' \
    'git-update-cache --remove $to_remove'

for p in M? Z/M?
do
    echo This is modified $p in the branch A. >$p
    test_expect_success \
	'change in branch A (modification)' \
        "git-update-cache $p"
done

for p in AN AA Z/AN Z/AA
do
    echo This is added $p in the branch A. >$p
    test_expect_success \
	'change in branch A (addition)' \
	"git-update-cache --add $p"
done

echo This is SS from the modified tree. >SS
echo This is LL from the modified tree. >LL
test_expect_success \
    'change in branch A (addition)' \
    'git-update-cache --add LL &&
     git-update-cache SS'
mv TT TT-
sed -e '/Branch A/s/word/WORD/g' <TT- >TT
rm -f TT-
test_expect_success \
    'change in branch A (edit)' \
    'git-update-cache TT'

mkdir DF
echo Branch A makes a file at DF/DF, creating a directory DF. >DF/DF
test_expect_success \
    'change in branch A (change file to directory)' \
    'git-update-cache --add DF/DF'

test_expect_success \
    'recording branch A tree' \
    'tree_A=$(git-write-tree)'
test_expect_success \
    'committing branch A changes' \
    'commit_A=$(echo "Branch A for the merge test." |
           git-commit-tree $tree_A -p $commit_O)'
echo $commit_A >.git/HEAD-A
	   
################################################################
# Branch B
# Start from O

rm -rf [NDMASLT][NDMASLT] Z DF
mkdir Z
test_expect_success \
    'reading original tree and checking out' \
    'git-read-tree $tree_O &&
     git-checkout-cache -a'

to_remove=$(echo ?D Z/?D)
rm -f $to_remove
test_expect_success \
    'change in branch B (removal)' \
    "git-update-cache --remove $to_remove"

for p in ?M Z/?M
do
    echo This is modified $p in the branch B. >$p
    test_expect_success \
	'change in branch B (modification)' \
	"git-update-cache $p"
done

for p in NA AA Z/NA Z/AA
do
    echo This is added $p in the branch B. >$p
    test_expect_success \
	'change in branch B (addition)' \
	"git-update-cache --add $p"
done
echo This is SS from the modified tree. >SS
echo This is LL from the modified tree. >LL
test_expect_success \
    'change in branch B (addition and modification)' \
    'git-update-cache --add LL &&
     git-update-cache SS'
mv TT TT-
sed -e '/Branch B/s/word/WORD/g' <TT- >TT
rm -f TT-
test_expect_success \
    'change in branch B (modification)' \
    'git-update-cache TT'

echo Branch B makes a file at DF. >DF
test_expect_success \
    'change in branch B (addition of a file to conflict with directory)' \
    'git-update-cache --add DF'

test_expect_success \
    'recording branch B tree' \
    'tree_B=$(git-write-tree)'
test_expect_success \
    'committing branch B changes' \
    'commit_B=$(echo "Branch B for the merge test." |
           git-commit-tree $tree_B -p $commit_O)'
echo $commit_B >.git/HEAD-B

################################################################
# Done preparation.

test_debug '
    for T in O A B
    do
	echo "# $T $(eval git-cat-file commit \$commit_$T | sed -e 1q)"
    done
'

################################################################
# Try merging and showing the various diffs

test_expect_success \
    '3-way merge with git-read-tree -m' \
    "git-read-tree -m $tree_O $tree_A $tree_B"

strip_object_id='s/^\([0-7]*\) [0-9a-f]* \([0-3].*\)$/\1 \2/'

test_expect_success \
    'git-ls-files --stage of the merge result' \
    'git-ls-files --stage >current- &&
     sed -e "$strip_object_id" <current- >current'

cat >expected <<\EOF
100644 2 AA
100644 3 AA
100644 2 AN
100644 1 DD
100644 3 DF
100644 2 DF/DF
100644 1 DM
100644 3 DM
100644 1 DN
100644 3 DN
100644 2 LL
100644 3 LL
100644 1 MD
100644 2 MD
100644 1 MM
100644 2 MM
100644 3 MM
100644 0 MN
100644 3 NA
100644 1 ND
100644 2 ND
100644 0 NM
100644 0 NN
100644 0 SS
100644 1 TT
100644 2 TT
100644 3 TT
100644 2 Z/AA
100644 3 Z/AA
100644 2 Z/AN
100644 1 Z/DD
100644 1 Z/DM
100644 3 Z/DM
100644 1 Z/DN
100644 3 Z/DN
100644 1 Z/MD
100644 2 Z/MD
100644 1 Z/MM
100644 2 Z/MM
100644 3 Z/MM
100644 0 Z/MN
100644 3 Z/NA
100644 1 Z/ND
100644 2 Z/ND
100644 0 Z/NM
100644 0 Z/NN
EOF

test_expect_success \
    'validate merge result' \
    'diff current expected'

test_done
