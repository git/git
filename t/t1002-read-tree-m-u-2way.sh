#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Two way merge with read-tree -m -u $H $M

This is identical to t1001, but uses -u to update the work tree as well.

'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-read-tree.sh

compare_change () {
	sed >current \
	    -e '1{/^diff --git /d;}' \
	    -e '2{/^index /d;}' \
	    -e '/^--- /d; /^+++ /d; /^@@ /d;' \
	    -e 's/^\(.[0-7][0-7][0-7][0-7][0-7][0-7]\) '"$OID_REGEX"' /\1 X /' "$1"
	test_cmp expected current
}

check_cache_at () {
	git diff-files -- "$1" >out &&
	clean_if_empty=$(cat out) &&
	case "$clean_if_empty" in
	'')  echo "$1: clean" ;;
	?*)  echo "$1: dirty" ;;
	esac &&
	case "$2,$clean_if_empty" in
	clean,)		:     ;;
	clean,?*)	false ;;
	dirty,)		false ;;
	dirty,?*)	:     ;;
	esac
}

test_expect_success setup '
	echo frotz >frotz &&
	echo nitfol >nitfol &&
	echo bozbar >bozbar &&
	echo rezrov >rezrov &&
	git update-index --add nitfol bozbar rezrov &&
	treeH=$(git write-tree) &&
	echo treeH $treeH &&
	git ls-tree $treeH &&

	echo gnusto >bozbar &&
	git update-index --add frotz bozbar --force-remove rezrov &&
	git ls-files --stage >M.out &&
	treeM=$(git write-tree) &&
	echo treeM $treeM &&
	git ls-tree $treeM &&
	cp bozbar bozbar.M &&
	cp frotz frotz.M &&
	cp nitfol nitfol.M &&
	git diff-tree $treeH $treeM
'

test_expect_success '1, 2, 3 - no carry forward' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	read_tree_u_must_succeed -m -u $treeH $treeM &&
	git ls-files --stage >1-3.out &&
	cmp M.out 1-3.out &&
	test_cmp bozbar.M bozbar &&
	test_cmp frotz.M frotz &&
	test_cmp nitfol.M nitfol &&
	check_cache_at bozbar clean &&
	check_cache_at frotz clean &&
	check_cache_at nitfol clean
'

test_expect_success '4 - carry forward local addition.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	echo "+100644 X 0	yomin" >expected &&
	echo yomin >yomin &&
	git update-index --add yomin &&
	read_tree_u_must_succeed -m -u $treeH $treeM &&
	git ls-files --stage >4.out &&
	test_might_fail git diff -U0 --no-index M.out 4.out >4diff.out &&
	compare_change 4diff.out expected &&
	check_cache_at yomin clean &&
	test_cmp bozbar.M bozbar &&
	test_cmp frotz.M frotz &&
	test_cmp nitfol.M nitfol &&
	echo yomin >yomin1 &&
	diff yomin yomin1 &&
	rm -f yomin1
'

test_expect_success '5 - carry forward local addition.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	read_tree_u_must_succeed -m -u $treeH &&
	echo yomin >yomin &&
	git update-index --add yomin &&
	echo yomin yomin >yomin &&
	read_tree_u_must_succeed -m -u $treeH $treeM &&
	git ls-files --stage >5.out &&
	test_might_fail git diff -U0 --no-index M.out 5.out >5diff.out &&
	compare_change 5diff.out expected &&
	check_cache_at yomin dirty &&
	test_cmp bozbar.M bozbar &&
	test_cmp frotz.M frotz &&
	test_cmp nitfol.M nitfol &&
	: dirty index should have prevented -u from checking it out. &&
	echo yomin yomin >yomin1 &&
	diff yomin yomin1 &&
	rm -f yomin1
'

test_expect_success '6 - local addition already has the same.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	echo frotz >frotz &&
	git update-index --add frotz &&
	read_tree_u_must_succeed -m -u $treeH $treeM &&
	git ls-files --stage >6.out &&
	test_cmp M.out 6.out &&
	check_cache_at frotz clean &&
	test_cmp bozbar.M bozbar &&
	test_cmp frotz.M frotz &&
	test_cmp nitfol.M nitfol &&
	echo frotz >frotz1 &&
	diff frotz frotz1 &&
	rm -f frotz1
'

test_expect_success '7 - local addition already has the same.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	echo frotz >frotz &&
	git update-index --add frotz &&
	echo frotz frotz >frotz &&
	read_tree_u_must_succeed -m -u $treeH $treeM &&
	git ls-files --stage >7.out &&
	test_cmp M.out 7.out &&
	check_cache_at frotz dirty &&
	test_cmp bozbar.M bozbar &&
	test_cmp nitfol.M nitfol &&
	: dirty index should have prevented -u from checking it out. &&
	echo frotz frotz >frotz1 &&
	diff frotz frotz1 &&
	rm -f frotz1
'

test_expect_success '8 - conflicting addition.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	echo frotz frotz >frotz &&
	git update-index --add frotz &&
	! read_tree_u_must_succeed -m -u $treeH $treeM
'

test_expect_success '9 - conflicting addition.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	echo frotz frotz >frotz &&
	git update-index --add frotz &&
	echo frotz >frotz &&
	! read_tree_u_must_succeed -m -u $treeH $treeM
'

test_expect_success '10 - path removed.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	echo rezrov >rezrov &&
	git update-index --add rezrov &&
	read_tree_u_must_succeed -m -u $treeH $treeM &&
	git ls-files --stage >10.out &&
	cmp M.out 10.out &&
	test_cmp bozbar.M bozbar &&
	test_cmp frotz.M frotz &&
	test_cmp nitfol.M nitfol
'

test_expect_success '11 - dirty path removed.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	echo rezrov >rezrov &&
	git update-index --add rezrov &&
	echo rezrov rezrov >rezrov &&
	! read_tree_u_must_succeed -m -u $treeH $treeM
'

test_expect_success '12 - unmatching local changes being removed.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	echo rezrov rezrov >rezrov &&
	git update-index --add rezrov &&
	! read_tree_u_must_succeed -m -u $treeH $treeM
'

test_expect_success '13 - unmatching local changes being removed.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	echo rezrov rezrov >rezrov &&
	git update-index --add rezrov &&
	echo rezrov >rezrov &&
	! read_tree_u_must_succeed -m -u $treeH $treeM
'

cat >expected <<EOF
-100644 X 0	nitfol
+100644 X 0	nitfol
EOF

test_expect_success '14 - unchanged in two heads.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	echo nitfol nitfol >nitfol &&
	git update-index --add nitfol &&
	read_tree_u_must_succeed -m -u $treeH $treeM &&
	git ls-files --stage >14.out &&
	test_must_fail git diff -U0 --no-index M.out 14.out >14diff.out &&
	compare_change 14diff.out expected &&
	test_cmp bozbar.M bozbar &&
	test_cmp frotz.M frotz &&
	check_cache_at nitfol clean &&
	echo nitfol nitfol >nitfol1 &&
	diff nitfol nitfol1 &&
	rm -f nitfol1
'

test_expect_success '15 - unchanged in two heads.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	echo nitfol nitfol >nitfol &&
	git update-index --add nitfol &&
	echo nitfol nitfol nitfol >nitfol &&
	read_tree_u_must_succeed -m -u $treeH $treeM &&
	git ls-files --stage >15.out &&
	test_must_fail git diff -U0 --no-index M.out 15.out >15diff.out &&
	compare_change 15diff.out expected &&
	check_cache_at nitfol dirty &&
	test_cmp bozbar.M bozbar &&
	test_cmp frotz.M frotz &&
	echo nitfol nitfol nitfol >nitfol1 &&
	diff nitfol nitfol1 &&
	rm -f nitfol1
'

test_expect_success '16 - conflicting local change.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	echo bozbar bozbar >bozbar &&
	git update-index --add bozbar &&
	! read_tree_u_must_succeed -m -u $treeH $treeM
'

test_expect_success '17 - conflicting local change.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	echo bozbar bozbar >bozbar &&
	git update-index --add bozbar &&
	echo bozbar bozbar bozbar >bozbar &&
	! read_tree_u_must_succeed -m -u $treeH $treeM
'

test_expect_success '18 - local change already having a good result.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	echo gnusto >bozbar &&
	git update-index --add bozbar &&
	read_tree_u_must_succeed -m -u $treeH $treeM &&
	git ls-files --stage >18.out &&
	test_cmp M.out 18.out &&
	check_cache_at bozbar clean &&
	test_cmp bozbar.M bozbar &&
	test_cmp frotz.M frotz &&
	test_cmp nitfol.M nitfol
'

test_expect_success '19 - local change already having a good result, further modified.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	echo gnusto >bozbar &&
	git update-index --add bozbar &&
	echo gnusto gnusto >bozbar &&
	read_tree_u_must_succeed -m -u $treeH $treeM &&
	git ls-files --stage >19.out &&
	test_cmp M.out 19.out &&
	check_cache_at bozbar dirty &&
	test_cmp frotz.M frotz &&
	test_cmp nitfol.M nitfol &&
	echo gnusto gnusto >bozbar1 &&
	diff bozbar bozbar1 &&
	rm -f bozbar1
'

test_expect_success '20 - no local change, use new tree.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	echo bozbar >bozbar &&
	git update-index --add bozbar &&
	read_tree_u_must_succeed -m -u $treeH $treeM &&
	git ls-files --stage >20.out &&
	test_cmp M.out 20.out &&
	check_cache_at bozbar clean &&
	test_cmp bozbar.M bozbar &&
	test_cmp frotz.M frotz &&
	test_cmp nitfol.M nitfol
'

test_expect_success '21 - no local change, dirty cache.' '
	rm -f .git/index nitfol bozbar rezrov frotz &&
	read_tree_u_must_succeed --reset -u $treeH &&
	echo bozbar >bozbar &&
	git update-index --add bozbar &&
	echo gnusto gnusto >bozbar &&
	! read_tree_u_must_succeed -m -u $treeH $treeM
'

# Also make sure we did not break DF vs DF/DF case.
test_expect_success 'DF vs DF/DF case setup.' '
	rm -f .git/index &&
	echo DF >DF &&
	git update-index --add DF &&
	treeDF=$(git write-tree) &&
	echo treeDF $treeDF &&
	git ls-tree $treeDF &&

	rm -f DF &&
	mkdir DF &&
	echo DF/DF >DF/DF &&
	git update-index --add --remove DF DF/DF &&
	treeDFDF=$(git write-tree) &&
	echo treeDFDF $treeDFDF &&
	git ls-tree $treeDFDF &&
	git ls-files --stage >DFDF.out
'

test_expect_success 'DF vs DF/DF case test.' '
	rm -f .git/index &&
	rm -fr DF &&
	echo DF >DF &&
	git update-index --add DF &&
	read_tree_u_must_succeed -m -u $treeDF $treeDFDF &&
	git ls-files --stage >DFDFcheck.out &&
	test_cmp DFDF.out DFDFcheck.out &&
	check_cache_at DF/DF clean
'

test_done
