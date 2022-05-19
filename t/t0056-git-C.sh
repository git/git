#!/bin/sh

test_description='"-C <path>" option and its effects on other path-related options'

. ./test-lib.sh

test_expect_success '"but -C <path>" runs but from the directory <path>' '
	test_create_repo dir1 &&
	echo 1 >dir1/a.txt &&
	msg="initial in dir1" &&
	(cd dir1 && but add a.txt && but cummit -m "$msg") &&
	echo "$msg" >expected &&
	but -C dir1 log --format=%s >actual &&
	test_cmp expected actual
'

test_expect_success '"but -C <path>" with an empty <path> is a no-op' '
	(
		mkdir -p dir1/subdir &&
		cd dir1/subdir &&
		but -C "" rev-parse --show-prefix >actual &&
		echo subdir/ >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'Multiple -C options: "-C dir1 -C dir2" is equivalent to "-C dir1/dir2"' '
	test_create_repo dir1/dir2 &&
	echo 1 >dir1/dir2/b.txt &&
	but -C dir1/dir2 add b.txt &&
	msg="initial in dir1/dir2" &&
	echo "$msg" >expected &&
	but -C dir1/dir2 cummit -m "$msg" &&
	but -C dir1 -C dir2 log --format=%s >actual &&
	test_cmp expected actual
'

test_expect_success 'Effect on --but-dir option: "-C c --but-dir=a.but" is equivalent to "--but-dir c/a.but"' '
	mkdir c &&
	mkdir c/a &&
	mkdir c/a.but &&
	(cd c/a.but && but init --bare) &&
	echo 1 >c/a/a.txt &&
	but --but-dir c/a.but --work-tree=c/a add a.txt &&
	but --but-dir c/a.but --work-tree=c/a cummit -m "initial" &&
	but --but-dir=c/a.but log -1 --format=%s >expected &&
	but -C c --but-dir=a.but log -1 --format=%s >actual &&
	test_cmp expected actual
'

test_expect_success 'Order should not matter: "--but-dir=a.but -C c" is equivalent to "-C c --but-dir=a.but"' '
	but -C c --but-dir=a.but log -1 --format=%s >expected &&
	but --but-dir=a.but -C c log -1 --format=%s >actual &&
	test_cmp expected actual
'

test_expect_success 'Effect on --work-tree option: "-C c/a.but --work-tree=../a"  is equivalent to "--work-tree=c/a --but-dir=c/a.but"' '
	rm c/a/a.txt &&
	but --but-dir=c/a.but --work-tree=c/a status >expected &&
	but -C c/a.but --work-tree=../a status >actual &&
	test_cmp expected actual
'

test_expect_success 'Order should not matter: "--work-tree=../a -C c/a.but" is equivalent to "-C c/a.but --work-tree=../a"' '
	but -C c/a.but --work-tree=../a status >expected &&
	but --work-tree=../a -C c/a.but status >actual &&
	test_cmp expected actual
'

test_expect_success 'Effect on --but-dir and --work-tree options - "-C c --but-dir=a.but --work-tree=a" is equivalent to "--but-dir=c/a.but --work-tree=c/a"' '
	but --but-dir=c/a.but --work-tree=c/a status >expected &&
	but -C c --but-dir=a.but --work-tree=a status >actual &&
	test_cmp expected actual
'

test_expect_success 'Order should not matter: "-C c --but-dir=a.but --work-tree=a" is equivalent to "--but-dir=a.but -C c --work-tree=a"' '
	but -C c --but-dir=a.but --work-tree=a status >expected &&
	but --but-dir=a.but -C c --work-tree=a status >actual &&
	test_cmp expected actual
'

test_expect_success 'Order should not matter: "-C c --but-dir=a.but --work-tree=a" is equivalent to "--but-dir=a.but --work-tree=a -C c"' '
	but -C c --but-dir=a.but --work-tree=a status >expected &&
	but --but-dir=a.but --work-tree=a -C c status >actual &&
	test_cmp expected actual
'

test_expect_success 'Relative followed by fullpath: "-C ./here -C /there" is equivalent to "-C /there"' '
	echo "initial in dir1/dir2" >expected &&
	but -C dir1 -C "$(pwd)/dir1/dir2" log --format=%s >actual &&
	test_cmp expected actual
'

test_done
