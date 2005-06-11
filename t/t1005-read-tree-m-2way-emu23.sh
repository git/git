#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Two way merge with read-tree --emu23 $H $M

This test tries two-way merge (aka fast forward with carry forward).

There is the head (called H) and another commit (called M), which is
simply ahead of H.  The index and the work tree contains a state that
is derived from H, but may also have local changes.  This test checks
all the combinations described in the two-tree merge "carry forward"
rules, found in <Documentation/git-rev-tree.txt>.

In the test, these paths are used:
        bozbar  - in H, stays in M, modified from bozbar to gnusto
        frotz   - not in H added in M
        nitfol  - in H, stays in M unmodified
        rezrov  - in H, deleted in M
        yomin   - not in H nor M
'
. ./test-lib.sh

read_tree_twoway () {
    git-read-tree --emu23 "$1" "$2" &&
    git-ls-files --stage &&
    git-merge-cache git-merge-one-file-script -a &&
    git-ls-files --stage
}

_x40='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x40="$_x40$_x40$_x40$_x40$_x40$_x40$_x40$_x40"
compare_change () {
    	cat current
	sed -n >current \
	    -e '/^--- /d; /^+++ /d; /^@@ /d;' \
	    -e 's/^\([-+][0-7][0-7][0-7][0-7][0-7][0-7]\) '"$_x40"' /\1 X /p' \
	    "$1"
	diff -u expected current
}

check_cache_at () {
	clean_if_empty=`git-diff-files "$1"`
	case "$clean_if_empty" in
	'')  echo "$1: clean" ;;
	?*)  echo "$1: dirty" ;;
	esac
	case "$2,$clean_if_empty" in
	clean,)		:     ;;
	clean,?*)	false ;;
	dirty,)		false ;;
	dirty,?*)	:     ;;
	esac
}

check_stages () {
    cat >expected_stages
    git-ls-files --stage | sed -e "s/ $_x40 / X /" >current_stages
    diff -u expected_stages current_stages
}

test_expect_success \
    setup \
    'echo frotz >frotz &&
     echo nitfol >nitfol &&
     echo bozbar >bozbar &&
     echo rezrov >rezrov &&
     echo yomin >yomin &&
     git-update-cache --add nitfol bozbar rezrov &&
     treeH=`git-write-tree` &&
     echo treeH $treeH &&
     git-ls-tree $treeH &&

     echo gnusto >bozbar &&
     git-update-cache --add frotz bozbar --force-remove rezrov &&
     git-ls-files --stage >M.out &&
     treeM=`git-write-tree` &&
     echo treeM $treeM &&
     git-ls-tree $treeM &&
     git-diff-tree $treeH $treeM'

test_expect_success \
    '1, 2, 3 - no carry forward' \
    'rm -f .git/index &&
     read_tree_twoway $treeH $treeM &&
     git-ls-files --stage >1-3.out &&
     diff -u M.out 1-3.out &&
     check_cache_at bozbar dirty &&
     check_cache_at frotz clean && # different from pure 2-way
     check_cache_at nitfol dirty'

echo '+100644 X 0	yomin' >expected

test_expect_success \
    '4 - carry forward local addition.' \
    'rm -f .git/index &&
     git-update-cache --add yomin &&
     read_tree_twoway $treeH $treeM &&
     git-ls-files --stage >4.out || exit
     diff -u M.out 4.out >4diff.out
     compare_change 4diff.out expected &&
     check_cache_at yomin clean'

# "read-tree -m H I+H M" where !H && !M; so (I+H) not being up-to-date
# should not matter, but without #3ALT this does not work.
: test_expect_success \
    '5 - carry forward local addition.' \
    'rm -f .git/index &&
     echo yomin >yomin &&
     git-update-cache --add yomin &&
     echo yomin yomin >yomin &&
     read_tree_twoway $treeH $treeM &&
     git-ls-files --stage >5.out || exit
     diff -u M.out 5.out >5diff.out
     compare_change 5diff.out expected &&
     check_cache_at yomin dirty'

# "read-tree -m H I+H M" where !H && M && (I+H) == M, so this should
# succeed (even the entry is clean), but without #5ALT this does not
# work.
: test_expect_success \
    '6 - local addition already has the same.' \
    'rm -f .git/index &&
     git-update-cache --add frotz &&
     read_tree_twoway $treeH $treeM &&
     git-ls-files --stage >6.out &&
     diff -u M.out 6.out &&
     check_cache_at frotz clean'

# Exactly the same pattern as above but with dirty cache.  This also
# should succeed, but without #5ALT it does not.
: test_expect_success \
    '7 - local addition already has the same.' \
    'rm -f .git/index &&
     echo frotz >frotz &&
     git-update-cache --add frotz &&
     echo frotz frotz >frotz &&
     read_tree_twoway $treeH $treeM &&
     git-ls-files --stage >7.out &&
     diff -u M.out 7.out &&
     check_cache_at frotz dirty'

test_expect_success \
    '8 - conflicting addition.' \
    'rm -f .git/index &&
     echo frotz frotz >frotz &&
     git-update-cache --add frotz &&
     if read_tree_twoway $treeH $treeM; then false; else :; fi'

test_expect_success \
    '9 - conflicting addition.' \
    'rm -f .git/index &&
     echo frotz frotz >frotz &&
     git-update-cache --add frotz &&
     echo frotz >frotz &&
     if read_tree_twoway $treeH $treeM; then false; else :; fi'

test_expect_success \
    '10 - path removed.' \
    'rm -f .git/index &&
     echo rezrov >rezrov &&
     git-update-cache --add rezrov &&
     read_tree_twoway $treeH $treeM &&
     git-ls-files --stage >10.out &&
     diff -u M.out 10.out'

test_expect_success \
    '11 - dirty path removed.' \
    'rm -f .git/index &&
     echo rezrov >rezrov &&
     git-update-cache --add rezrov &&
     echo rezrov rezrov >rezrov &&
     if read_tree_twoway $treeH $treeM; then false; else :; fi'

test_expect_success \
    '12 - unmatching local changes being removed.' \
    'rm -f .git/index &&
     echo rezrov rezrov >rezrov &&
     git-update-cache --add rezrov &&
     if read_tree_twoway $treeH $treeM; then false; else :; fi'

test_expect_success \
    '13 - unmatching local changes being removed.' \
    'rm -f .git/index &&
     echo rezrov rezrov >rezrov &&
     git-update-cache --add rezrov &&
     echo rezrov >rezrov &&
     if read_tree_twoway $treeH $treeM; then false; else :; fi'

cat >expected <<EOF
-100644 X 0	nitfol
+100644 X 0	nitfol
EOF

test_expect_success \
    '14 - unchanged in two heads.' \
    'rm -f .git/index &&
     echo nitfol nitfol >nitfol &&
     git-update-cache --add nitfol &&
     read_tree_twoway $treeH $treeM &&
     git-ls-files --stage >14.out || exit
     diff -u M.out 14.out >14diff.out
     compare_change 14diff.out expected &&
     check_cache_at nitfol clean'

test_expect_success \
    '15 - unchanged in two heads.' \
    'rm -f .git/index &&
     echo nitfol nitfol >nitfol &&
     git-update-cache --add nitfol &&
     echo nitfol nitfol nitfol >nitfol &&
     read_tree_twoway $treeH $treeM &&
     git-ls-files --stage >15.out || exit
     diff -u M.out 15.out >15diff.out
     compare_change 15diff.out expected &&
     check_cache_at nitfol dirty'

# This is different from straight 2-way merge in that it leaves
# three stages of bozbar in the index file without failing, so
# the user can run git-diff-stages to examine the situation.
test_expect_success \
    '16 - conflicting local change.' \
    'rm -f .git/index &&
     echo bozbar bozbar >bozbar &&
     git-update-cache --add bozbar &&
     git-read-tree --emu23 $treeH $treeM &&
     check_stages' <<\EOF
100644 X 1	bozbar
100644 X 2	bozbar
100644 X 3	bozbar
100644 X 3	frotz
100644 X 0	nitfol
100644 X 1	rezrov
100644 X 2	rezrov
EOF

test_expect_success \
    '17 - conflicting local change.' \
    'rm -f .git/index &&
     echo bozbar bozbar >bozbar &&
     git-update-cache --add bozbar &&
     echo bozbar bozbar bozbar >bozbar &&
     if read_tree_twoway $treeH $treeM; then false; else :; fi'

test_expect_success \
    '18 - local change already having a good result.' \
    'rm -f .git/index &&
     echo gnusto >bozbar &&
     git-update-cache --add bozbar &&
     read_tree_twoway $treeH $treeM &&
     git-ls-files --stage >18.out &&
     diff -u M.out 18.out &&
     check_cache_at bozbar clean'

test_expect_success \
    '19 - local change already having a good result, further modified.' \
    'rm -f .git/index &&
     echo gnusto >bozbar &&
     git-update-cache --add bozbar &&
     echo gnusto gnusto >bozbar &&
     read_tree_twoway $treeH $treeM &&
     git-ls-files --stage >19.out &&
     diff -u M.out 19.out &&
     check_cache_at bozbar dirty'

test_expect_success \
    '20 - no local change, use new tree.' \
    'rm -f .git/index &&
     echo bozbar >bozbar &&
     git-update-cache --add bozbar &&
     read_tree_twoway $treeH $treeM &&
     git-ls-files --stage >20.out &&
     diff -u M.out 20.out &&
     check_cache_at bozbar dirty'

test_expect_success \
    '21 - no local change, dirty cache.' \
    'rm -f .git/index &&
     echo bozbar >bozbar &&
     git-update-cache --add bozbar &&
     echo gnusto gnusto >bozbar &&
     if read_tree_twoway $treeH $treeM; then false; else :; fi'

# Also make sure we did not break DF vs DF/DF case.
test_expect_success \
    'DF vs DF/DF case setup.' \
    'rm -f .git/index &&
     echo DF >DF &&
     git-update-cache --add DF &&
     treeDF=`git-write-tree` &&
     echo treeDF $treeDF &&
     git-ls-tree $treeDF &&

     rm -f DF &&
     mkdir DF &&
     echo DF/DF >DF/DF &&
     git-update-cache --add --remove DF DF/DF &&
     treeDFDF=`git-write-tree` &&
     echo treeDFDF $treeDFDF &&
     git-ls-tree $treeDFDF &&
     git-ls-files --stage >DFDF.out'

test_expect_success \
    'DF vs DF/DF case test.' \
    'rm -f .git/index &&
     rm -fr DF &&
     echo DF >DF &&
     git-update-cache --add DF &&
     read_tree_twoway $treeDF $treeDFDF &&
     git-ls-files --stage >DFDFcheck.out &&
     diff -u DFDF.out DFDFcheck.out &&
     check_cache_at DF/DF clean && # different from pure 2-way
     :'

test_done
