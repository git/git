#!/bin/sh

test_description='"-C <path>" option and its effects on other path-related options'

. ./test-lib.sh

test_expect_success '"git -C <path>" runs git from the directory <path>' '
	test_create_repo dir1 &&
	echo 1 >dir1/a.txt &&
	msg="initial in dir1" &&
	(cd dir1 && git add a.txt && git commit -m "$msg") &&
	echo "$msg" >expected &&
	git -C dir1 log --format=%s >actual &&
	test_cmp expected actual
'

test_expect_success '"git -C <path>" with an empty <path> is a no-op' '
	(
		mkdir -p dir1/subdir &&
		cd dir1/subdir &&
		git -C "" rev-parse --show-prefix >actual &&
		echo subdir/ >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'Multiple -C options: "-C dir1 -C dir2" is equivalent to "-C dir1/dir2"' '
	test_create_repo dir1/dir2 &&
	echo 1 >dir1/dir2/b.txt &&
	git -C dir1/dir2 add b.txt &&
	msg="initial in dir1/dir2" &&
	echo "$msg" >expected &&
	git -C dir1/dir2 commit -m "$msg" &&
	git -C dir1 -C dir2 log --format=%s >actual &&
	test_cmp expected actual
'

test_expect_success 'Effect on --git-dir option: "-C c --git-dir=a.git" is equivalent to "--git-dir c/a.git"' '
	mkdir c &&
	mkdir c/a &&
	mkdir c/a.git &&
	(cd c/a.git && git init --bare) &&
	echo 1 >c/a/a.txt &&
	git --git-dir c/a.git --work-tree=c/a add a.txt &&
	git --git-dir c/a.git --work-tree=c/a commit -m "initial" &&
	git --git-dir=c/a.git log -1 --format=%s >expected &&
	git -C c --git-dir=a.git log -1 --format=%s >actual &&
	test_cmp expected actual
'

test_expect_success 'Order should not matter: "--git-dir=a.git -C c" is equivalent to "-C c --git-dir=a.git"' '
	git -C c --git-dir=a.git log -1 --format=%s >expected &&
	git --git-dir=a.git -C c log -1 --format=%s >actual &&
	test_cmp expected actual
'

test_expect_success 'Effect on --work-tree option: "-C c/a.git --work-tree=../a"  is equivalent to "--work-tree=c/a --git-dir=c/a.git"' '
	rm c/a/a.txt &&
	git --git-dir=c/a.git --work-tree=c/a status >expected &&
	git -C c/a.git --work-tree=../a status >actual &&
	test_cmp expected actual
'

test_expect_success 'Order should not matter: "--work-tree=../a -C c/a.git" is equivalent to "-C c/a.git --work-tree=../a"' '
	git -C c/a.git --work-tree=../a status >expected &&
	git --work-tree=../a -C c/a.git status >actual &&
	test_cmp expected actual
'

test_expect_success 'Effect on --git-dir and --work-tree options - "-C c --git-dir=a.git --work-tree=a" is equivalent to "--git-dir=c/a.git --work-tree=c/a"' '
	git --git-dir=c/a.git --work-tree=c/a status >expected &&
	git -C c --git-dir=a.git --work-tree=a status >actual &&
	test_cmp expected actual
'

test_expect_success 'Order should not matter: "-C c --git-dir=a.git --work-tree=a" is equivalent to "--git-dir=a.git -C c --work-tree=a"' '
	git -C c --git-dir=a.git --work-tree=a status >expected &&
	git --git-dir=a.git -C c --work-tree=a status >actual &&
	test_cmp expected actual
'

test_expect_success 'Order should not matter: "-C c --git-dir=a.git --work-tree=a" is equivalent to "--git-dir=a.git --work-tree=a -C c"' '
	git -C c --git-dir=a.git --work-tree=a status >expected &&
	git --git-dir=a.git --work-tree=a -C c status >actual &&
	test_cmp expected actual
'

test_expect_success 'Relative followed by fullpath: "-C ./here -C /there" is equivalent to "-C /there"' '
	echo "initial in dir1/dir2" >expected &&
	git -C dir1 -C "$(pwd)/dir1/dir2" log --format=%s >actual &&
	test_cmp expected actual
'

test_done
