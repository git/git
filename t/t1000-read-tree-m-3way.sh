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
# This is the "no trivial merge unless all three exists" table.

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

_x40='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x40="$_x40$_x40$_x40$_x40$_x40$_x40$_x40$_x40"

check_result () {
    git-ls-files --stage | sed -e 's/ '"$_x40"' / X /' >current &&
    diff -u expected current
}

# This is done on an empty work directory, which is the normal
# merge person behaviour.
test_expect_success \
    '3-way merge with git-read-tree -m, empty cache' \
    "rm -fr [NDMALTS][NDMALTSF] Z &&
     rm .git/index &&
     git-read-tree -m $tree_O $tree_A $tree_B &&
     check_result"

# This starts out with the first head, which is the normal
# patch submitter behaviour.
test_expect_success \
    '3-way merge with git-read-tree -m, match H' \
    "rm -fr [NDMALTS][NDMALTSF] Z &&
     rm .git/index &&
     git-read-tree $tree_A &&
     git-checkout-cache -f -u -a &&
     git-read-tree -m $tree_O $tree_A $tree_B &&
     check_result"

: <<\END_OF_CASE_TABLE

We have so far tested only empty index and clean-and-matching-A index
case which are trivial.  Make sure index requirements are also
checked.  The table also lists alternative semantics which is not
currently implemented.

"git-diff-tree -m O A B"

     O       A       B         result      index requirements
-------------------------------------------------------------------
  1  missing missing missing   -           must not exist.
 ------------------------------------------------------------------
  2  missing missing exists    no merge    must not exist.
                               ------------------------------------
    (ALT)                      take B*     must match B, if exists.
 ------------------------------------------------------------------
  3  missing exists  missing   no merge    must match A and be
                                           up-to-date, if exists.
                               ------------------------------------
    (ALT)                      take A*     must match A, if exists.
 ------------------------------------------------------------------
  4  missing exists  A!=B      no merge    must match A and be
                                           up-to-date, if exists.
 ------------------------------------------------------------------
  5  missing exists  A==B      no merge    must match A and be
                                           up-to-date, if exists.
                               ------------------------------------
    (ALT)                      take A      must match A, if exists.
 ------------------------------------------------------------------
  6  exists  missing missing   no merge    must not exist.
                               ------------------------------------
    (ALT)                      remove      must not exist.
 ------------------------------------------------------------------
  7  exists  missing O!=B      no merge    must not exist.
 ------------------------------------------------------------------
  8  exists  missing O==B      no merge    must not exist.
                               ------------------------------------
    (ALT)                      remove      must not exist.
 ------------------------------------------------------------------
  9  exists  O!=A    missing   no merge    must match A and be
                                           up-to-date, if exists.
 ------------------------------------------------------------------
 10  exists  O==A    missing   no merge    must match A and be
                                           up-to-date, if exists.
                               ------------------------------------
    (ALT)                      remove      ditto
 ------------------------------------------------------------------
 11  exists  O!=A    O!=B      no merge    must match A and be
                     A!=B                  up-to-date, if exists.
 ------------------------------------------------------------------
 12  exists  O!=A    O!=B      take A      must match A, if exists.
                     A==B
 ------------------------------------------------------------------
 13  exists  O!=A    O==B      take A      must match A, if exists.
 ------------------------------------------------------------------
 14  exists  O==A    O!=B      take B      must match A and be
                                           be up-to-date, if exists.
                               ------------------------------------
    (ALT)                      take B      if exists, must either (1)
                                           match A and be up-to-date,
                                           or (2) match B.
 ------------------------------------------------------------------
 15  exists  O==A    O==B      take B      must match A if exists.
-------------------------------------------------------------------

Note: if we want to implement 2ALT and 3ALT we need to be careful.
The tree A may contain DF (file) when tree B require DF to be a
directory by having DF/DF (file).

END_OF_CASE_TABLE

test_expect_failure \
    '1 - must not have an entry not in A.' \
    "rm -f .git/index XX &&
     echo XX >XX &&
     git-update-cache --add XX &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_failure \
    '2 - must not have an entry not in A.' \
    "rm -f .git/index NA &&
     cp .orig-B/NA NA &&
     git-update-cache --add NA &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_success \
    '3 - must match and be up-to-date in !O && A && !B case.' \
    "rm -f .git/index AN &&
     cp .orig-A/AN AN &&
     git-update-cache --add AN &&
     git-read-tree -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_failure \
    '3 (fail) - must match and be up-to-date in !O && A && !B case.' \
    "rm -f .git/index AN &&
     cp .orig-A/AN AN &&
     git-update-cache --add AN &&
     echo extra >>AN &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_failure \
    '3 (fail) - must match and be up-to-date in !O && A && !B case.' \
    "rm -f .git/index AN &&
     cp .orig-A/AN AN &&
     echo extra >>AN &&
     git-update-cache --add AN &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_success \
    '4 - must match and be up-to-date in !O && A && B && A!=B case.' \
    "rm -f .git/index AA &&
     cp .orig-A/AA AA &&
     git-update-cache --add AA &&
     git-read-tree -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_failure \
    '4 (fail) - must match and be up-to-date in !O && A && B && A!=B case.' \
    "rm -f .git/index AA &&
     cp .orig-A/AA AA &&
     git-update-cache --add AA &&
     echo extra >>AA &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_failure \
    '4 (fail) - must match and be up-to-date in !O && A && B && A!=B case.' \
    "rm -f .git/index AA &&
     cp .orig-A/AA AA &&
     echo extra >>AA &&
     git-update-cache --add AA &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_success \
    '5 - must match and be up-to-date in !O && A && B && A==B case.' \
    "rm -f .git/index LL &&
     cp .orig-A/LL LL &&
     git-update-cache --add LL &&
     git-read-tree -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_failure \
    '5 (fail) - must match and be up-to-date in !O && A && B && A==B case.' \
    "rm -f .git/index LL &&
     cp .orig-A/LL LL &&
     git-update-cache --add LL &&
     echo extra >>LL &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_failure \
    '5 (fail) - must match and be up-to-date in !O && A && B && A==B case.' \
    "rm -f .git/index LL &&
     cp .orig-A/LL LL &&
     echo extra >>LL &&
     git-update-cache --add LL &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_failure \
    '6 - must not exist in O && !A && !B case' \
    "rm -f .git/index DD &&
     echo DD >DD
     git-update-cache --add DD &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_failure \
    '7 - must not exist in O && !A && B && O!=B case' \
    "rm -f .git/index DM &&
     cp .orig-B/DM DM &&
     git-update-cache --add DM &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_failure \
    '8 - must not exist in O && !A && B && O==B case' \
    "rm -f .git/index DN &&
     cp .orig-B/DN DN &&
     git-update-cache --add DN &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_success \
    '9 - must match and be up-to-date in O && A && !B && O!=A case' \
    "rm -f .git/index MD &&
     cp .orig-A/MD MD &&
     git-update-cache --add MD &&
     git-read-tree -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_failure \
    '9 (fail) - must match and be up-to-date in O && A && !B && O!=A case' \
    "rm -f .git/index MD &&
     cp .orig-A/MD MD &&
     git-update-cache --add MD &&
     echo extra >>MD &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_failure \
    '9 (fail) - must match and be up-to-date in O && A && !B && O!=A case' \
    "rm -f .git/index MD &&
     cp .orig-A/MD MD &&
     echo extra >>MD &&
     git-update-cache --add MD &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_success \
    '10 - must match and be up-to-date in O && A && !B && O==A case' \
    "rm -f .git/index ND &&
     cp .orig-A/ND ND &&
     git-update-cache --add ND &&
     git-read-tree -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_failure \
    '10 (fail) - must match and be up-to-date in O && A && !B && O==A case' \
    "rm -f .git/index ND &&
     cp .orig-A/ND ND &&
     git-update-cache --add ND &&
     echo extra >>ND &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_failure \
    '10 (fail) - must match and be up-to-date in O && A && !B && O==A case' \
    "rm -f .git/index ND &&
     cp .orig-A/ND ND &&
     echo extra >>ND &&
     git-update-cache --add ND &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_success \
    '11 - must match and be up-to-date in O && A && B && O!=A && O!=B && A!=B case' \
    "rm -f .git/index MM &&
     cp .orig-A/MM MM &&
     git-update-cache --add MM &&
     git-read-tree -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_failure \
    '11 (fail) - must match and be up-to-date in O && A && B && O!=A && O!=B && A!=B case' \
    "rm -f .git/index MM &&
     cp .orig-A/MM MM &&
     git-update-cache --add MM &&
     echo extra >>MM &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_failure \
    '11 (fail) - must match and be up-to-date in O && A && B && O!=A && O!=B && A!=B case' \
    "rm -f .git/index MM &&
     cp .orig-A/MM MM &&
     echo extra >>MM &&
     git-update-cache --add MM &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_success \
    '12 - must match A in O && A && B && O!=A && A==B case' \
    "rm -f .git/index SS &&
     cp .orig-A/SS SS &&
     git-update-cache --add SS &&
     git-read-tree -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '12 - must match A in O && A && B && O!=A && A==B case' \
    "rm -f .git/index SS &&
     cp .orig-A/SS SS &&
     git-update-cache --add SS &&
     echo extra >>SS &&
     git-read-tree -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_failure \
    '12 (fail) - must match A in O && A && B && O!=A && A==B case' \
    "rm -f .git/index SS &&
     cp .orig-A/SS SS &&
     echo extra >>SS &&
     git-update-cache --add SS &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_success \
    '13 - must match A in O && A && B && O!=A && O==B case' \
    "rm -f .git/index MN &&
     cp .orig-A/MN MN &&
     git-update-cache --add MN &&
     git-read-tree -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '13 - must match A in O && A && B && O!=A && O==B case' \
    "rm -f .git/index MN &&
     cp .orig-A/MN MN &&
     git-update-cache --add MN &&
     echo extra >>MN &&
     git-read-tree -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '14 - must match and be up-to-date in O && A && B && O==A && O!=B case' \
    "rm -f .git/index NM &&
     cp .orig-A/NM NM &&
     git-update-cache --add NM &&
     git-read-tree -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_failure \
    '14 (fail) - must match and be up-to-date in O && A && B && O==A && O!=B case' \
    "rm -f .git/index NM &&
     cp .orig-A/NM NM &&
     git-update-cache --add NM &&
     echo extra >>NM &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_failure \
    '14 (fail) - must match and be up-to-date in O && A && B && O==A && O!=B case' \
    "rm -f .git/index NM &&
     cp .orig-A/NM NM &&
     echo extra >>NM &&
     git-update-cache --add NM &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_expect_success \
    '15 - must match A in O && A && B && O==A && O==B case' \
    "rm -f .git/index NN &&
     cp .orig-A/NN NN &&
     git-update-cache --add NN &&
     git-read-tree -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_success \
    '15 - must match A in O && A && B && O==A && O==B case' \
    "rm -f .git/index NN &&
     cp .orig-A/NN NN &&
     git-update-cache --add NN &&
     echo extra >>NN &&
     git-read-tree -m $tree_O $tree_A $tree_B &&
     check_result"

test_expect_failure \
    '15 (fail) - must match A in O && A && B && O==A && O==B case' \
    "rm -f .git/index NN &&
     cp .orig-A/NN NN &&
     echo extra >>NN &&
     git-update-cache --add NN &&
     git-read-tree -m $tree_O $tree_A $tree_B"

test_done
