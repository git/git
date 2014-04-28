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
. "$TEST_DIRECTORY"/lib-read-tree.sh
. "$TEST_DIRECTORY"/lib-read-tree-m-3way.sh

################################################################
# Trivial "majority when 3 stages exist" merge plus #2ALT, #3ALT
# and #5ALT trivial merges.

cat >expected <<\EOF
100644 X 2	AA
100644 X 3	AA
100644 X 0	AN
100644 X 1	DD
100644 X 3	DF
100644 X 2	DF/DF
100644 X 1	DM
100644 X 3	DM
100644 X 1	DN
100644 X 3	DN
100644 X 0	LL
100644 X 1	MD
100644 X 2	MD
100644 X 1	MM
100644 X 2	MM
100644 X 3	MM
100644 X 0	MN
100644 X 0	NA
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
100644 X 0	Z/AN
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
100644 X 0	Z/NA
100644 X 1	Z/ND
100644 X 2	Z/ND
100644 X 0	Z/NM
100644 X 0	Z/NN
EOF

check_result () {
    git ls-files --stage | sed -e 's/ '"$_x40"' / X /' >current &&
    test_cmp expected current
}

# This is done on an empty work directory, which is the normal
# merge person behaviour.
test_expect_success \
    '3-way merge with git read-tree -m, empty cache' \
    "rm -fr [NDMALTS][NDMALTSF] Z &&
     rm .git/index &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B &&
     check_result"

# This starts out with the first head, which is the normal
# patch submitter behaviour.
test_expect_success \
    '3-way merge with git read-tree -m, match H' \
    "rm -fr [NDMALTS][NDMALTSF] Z &&
     rm .git/index &&
     read_tree_must_succeed $tree_A &&
     git checkout-index -f -u -a &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B &&
     check_result"

: <<\END_OF_CASE_TABLE

We have so far tested only empty index and clean-and-matching-A index
case which are trivial.  Make sure index requirements are also
checked.

"git read-tree -m O A B"

     O       A       B         result      index requirements
-------------------------------------------------------------------
  1  missing missing missing   -           must not exist.
 ------------------------------------------------------------------
  2  missing missing exists    take B*     must match B, if exists.
 ------------------------------------------------------------------
  3  missing exists  missing   take A*     must match A, if exists.
 ------------------------------------------------------------------
  4  missing exists  A!=B      no merge    must match A and be
                                           up-to-date, if exists.
 ------------------------------------------------------------------
  5  missing exists  A==B      take A      must match A, if exists.
 ------------------------------------------------------------------
  6  exists  missing missing   remove      must not exist.
 ------------------------------------------------------------------
  7  exists  missing O!=B      no merge    must not exist.
 ------------------------------------------------------------------
  8  exists  missing O==B      remove      must not exist.
 ------------------------------------------------------------------
  9  exists  O!=A    missing   no merge    must match A and be
                                           up-to-date, if exists.
 ------------------------------------------------------------------
 10  exists  O==A    missing   no merge    must match A
 ------------------------------------------------------------------
 11  exists  O!=A    O!=B      no merge    must match A and be
                     A!=B                  up-to-date, if exists.
 ------------------------------------------------------------------
 12  exists  O!=A    O!=B      take A      must match A, if exists.
                     A==B
 ------------------------------------------------------------------
 13  exists  O!=A    O==B      take A      must match A, if exists.
 ------------------------------------------------------------------
 14  exists  O==A    O!=B      take B      if exists, must either (1)
                                           match A and be up-to-date,
                                           or (2) match B.
 ------------------------------------------------------------------
 15  exists  O==A    O==B      take B      must match A if exists.
 ------------------------------------------------------------------
 16  exists  O==A    O==B      barf        must match A if exists.
     *multi* in one  in another
-------------------------------------------------------------------

Note: we need to be careful in case 2 and 3.  The tree A may contain
DF (file) when tree B require DF to be a directory by having DF/DF
(file).

END_OF_CASE_TABLE

test_expect_success '1 - must not have an entry not in A.' "
     rm -f .git/index XX &&
     echo XX >XX &&
     git update-index --add XX &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

test_expect_success \
    '2 - must match B in !O && !A && B case.' \
    "rm -f .git/index NA &&
     cp .orig-B/NA NA &&
     git update-index --add NA &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B"

test_expect_success \
    '2 - matching B alone is OK in !O && !A && B case.' \
    "rm -f .git/index NA &&
     cp .orig-B/NA NA &&
     git update-index --add NA &&
     echo extra >>NA &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B"

test_expect_success \
    '3 - must match A in !O && A && !B case.' \
    "rm -f .git/index AN &&
     cp .orig-A/AN AN &&
     git update-index --add AN &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '3 - matching A alone is OK in !O && A && !B case.' \
    "rm -f .git/index AN &&
     cp .orig-A/AN AN &&
     git update-index --add AN &&
     echo extra >>AN &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B"

test_expect_success \
    '3 (fail) - must match A in !O && A && !B case.' "
     rm -f .git/index AN &&
     cp .orig-A/AN AN &&
     echo extra >>AN &&
     git update-index --add AN &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

test_expect_success \
    '4 - must match and be up-to-date in !O && A && B && A!=B case.' \
    "rm -f .git/index AA &&
     cp .orig-A/AA AA &&
     git update-index --add AA &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '4 (fail) - must match and be up-to-date in !O && A && B && A!=B case.' "
     rm -f .git/index AA &&
     cp .orig-A/AA AA &&
     git update-index --add AA &&
     echo extra >>AA &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

test_expect_success \
    '4 (fail) - must match and be up-to-date in !O && A && B && A!=B case.' "
     rm -f .git/index AA &&
     cp .orig-A/AA AA &&
     echo extra >>AA &&
     git update-index --add AA &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

test_expect_success \
    '5 - must match in !O && A && B && A==B case.' \
    "rm -f .git/index LL &&
     cp .orig-A/LL LL &&
     git update-index --add LL &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '5 - must match in !O && A && B && A==B case.' \
    "rm -f .git/index LL &&
     cp .orig-A/LL LL &&
     git update-index --add LL &&
     echo extra >>LL &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '5 (fail) - must match A in !O && A && B && A==B case.' "
     rm -f .git/index LL &&
     cp .orig-A/LL LL &&
     echo extra >>LL &&
     git update-index --add LL &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

test_expect_success \
    '6 - must not exist in O && !A && !B case' "
     rm -f .git/index DD &&
     echo DD >DD &&
     git update-index --add DD &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

test_expect_success \
    '7 - must not exist in O && !A && B && O!=B case' "
     rm -f .git/index DM &&
     cp .orig-B/DM DM &&
     git update-index --add DM &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

test_expect_success \
    '8 - must not exist in O && !A && B && O==B case' "
     rm -f .git/index DN &&
     cp .orig-B/DN DN &&
     git update-index --add DN &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

test_expect_success \
    '9 - must match and be up-to-date in O && A && !B && O!=A case' \
    "rm -f .git/index MD &&
     cp .orig-A/MD MD &&
     git update-index --add MD &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '9 (fail) - must match and be up-to-date in O && A && !B && O!=A case' "
     rm -f .git/index MD &&
     cp .orig-A/MD MD &&
     git update-index --add MD &&
     echo extra >>MD &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

test_expect_success \
    '9 (fail) - must match and be up-to-date in O && A && !B && O!=A case' "
     rm -f .git/index MD &&
     cp .orig-A/MD MD &&
     echo extra >>MD &&
     git update-index --add MD &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

test_expect_success \
    '10 - must match and be up-to-date in O && A && !B && O==A case' \
    "rm -f .git/index ND &&
     cp .orig-A/ND ND &&
     git update-index --add ND &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '10 (fail) - must match and be up-to-date in O && A && !B && O==A case' "
     rm -f .git/index ND &&
     cp .orig-A/ND ND &&
     git update-index --add ND &&
     echo extra >>ND &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

test_expect_success \
    '10 (fail) - must match and be up-to-date in O && A && !B && O==A case' "
     rm -f .git/index ND &&
     cp .orig-A/ND ND &&
     echo extra >>ND &&
     git update-index --add ND &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

test_expect_success \
    '11 - must match and be up-to-date in O && A && B && O!=A && O!=B && A!=B case' \
    "rm -f .git/index MM &&
     cp .orig-A/MM MM &&
     git update-index --add MM &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '11 (fail) - must match and be up-to-date in O && A && B && O!=A && O!=B && A!=B case' "
     rm -f .git/index MM &&
     cp .orig-A/MM MM &&
     git update-index --add MM &&
     echo extra >>MM &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

test_expect_success \
    '11 (fail) - must match and be up-to-date in O && A && B && O!=A && O!=B && A!=B case' "
     rm -f .git/index MM &&
     cp .orig-A/MM MM &&
     echo extra >>MM &&
     git update-index --add MM &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

test_expect_success \
    '12 - must match A in O && A && B && O!=A && A==B case' \
    "rm -f .git/index SS &&
     cp .orig-A/SS SS &&
     git update-index --add SS &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '12 - must match A in O && A && B && O!=A && A==B case' \
    "rm -f .git/index SS &&
     cp .orig-A/SS SS &&
     git update-index --add SS &&
     echo extra >>SS &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '12 (fail) - must match A in O && A && B && O!=A && A==B case' "
     rm -f .git/index SS &&
     cp .orig-A/SS SS &&
     echo extra >>SS &&
     git update-index --add SS &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

test_expect_success \
    '13 - must match A in O && A && B && O!=A && O==B case' \
    "rm -f .git/index MN &&
     cp .orig-A/MN MN &&
     git update-index --add MN &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '13 - must match A in O && A && B && O!=A && O==B case' \
    "rm -f .git/index MN &&
     cp .orig-A/MN MN &&
     git update-index --add MN &&
     echo extra >>MN &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '14 - must match and be up-to-date in O && A && B && O==A && O!=B case' \
    "rm -f .git/index NM &&
     cp .orig-A/NM NM &&
     git update-index --add NM &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '14 - may match B in O && A && B && O==A && O!=B case' \
    "rm -f .git/index NM &&
     cp .orig-B/NM NM &&
     git update-index --add NM &&
     echo extra >>NM &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '14 (fail) - must match and be up-to-date in O && A && B && O==A && O!=B case' "
     rm -f .git/index NM &&
     cp .orig-A/NM NM &&
     git update-index --add NM &&
     echo extra >>NM &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

test_expect_success \
    '14 (fail) - must match and be up-to-date in O && A && B && O==A && O!=B case' "
     rm -f .git/index NM &&
     cp .orig-A/NM NM &&
     echo extra >>NM &&
     git update-index --add NM &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

test_expect_success \
    '15 - must match A in O && A && B && O==A && O==B case' \
    "rm -f .git/index NN &&
     cp .orig-A/NN NN &&
     git update-index --add NN &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '15 - must match A in O && A && B && O==A && O==B case' \
    "rm -f .git/index NN &&
     cp .orig-A/NN NN &&
     git update-index --add NN &&
     echo extra >>NN &&
     read_tree_must_succeed -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '15 (fail) - must match A in O && A && B && O==A && O==B case' "
     rm -f .git/index NN &&
     cp .orig-A/NN NN &&
     echo extra >>NN &&
     git update-index --add NN &&
     read_tree_must_fail -m $tree_O $tree_A $tree_B
"

# #16
test_expect_success \
    '16 - A matches in one and B matches in another.' \
    'rm -f .git/index F16 &&
    echo F16 >F16 &&
    git update-index --add F16 &&
    tree0=$(git write-tree) &&
    echo E16 >F16 &&
    git update-index F16 &&
    tree1=$(git write-tree) &&
    read_tree_must_succeed -m $tree0 $tree1 $tree1 $tree0 &&
    git ls-files --stage'

test_done
