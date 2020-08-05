#!/bin/sh

test_description='subtree merge strategy'

. ./test-lib.sh

test_expect_success setup '

	s="1 2 3 4 5 6 7 8" &&
	for i in $s; do echo $i; done >hello &&
	git add hello &&
	git commit -m initial &&
	git checkout -b side &&
	echo >>hello world &&
	git add hello &&
	git commit -m second &&
	git checkout master &&
	for i in mundo $s; do echo $i; done >hello &&
	git add hello &&
	git commit -m master

'

test_expect_success 'subtree available and works like recursive' '

	git merge -s subtree side &&
	for i in mundo $s world; do echo $i; done >expect &&
	test_cmp expect hello

'

test_expect_success 'setup branch sub' '
	git checkout --orphan sub &&
	git rm -rf . &&
	test_commit foo
'

test_expect_success 'setup branch main' '
	git checkout -b main master &&
	git merge -s ours --no-commit --allow-unrelated-histories sub &&
	git read-tree --prefix=dir/ -u sub &&
	git commit -m "initial merge of sub into main" &&
	test_path_is_file dir/foo.t &&
	test_path_is_file hello
'

test_expect_success 'update branch sub' '
	git checkout sub &&
	test_commit bar
'

test_expect_success 'update branch main' '
	git checkout main &&
	git merge -s subtree sub -m "second merge of sub into main" &&
	test_path_is_file dir/bar.t &&
	test_path_is_file dir/foo.t &&
	test_path_is_file hello
'

test_expect_success 'setup' '
	mkdir git-gui &&
	cd git-gui &&
	git init &&
	echo git-gui > git-gui.sh &&
	o1=$(git hash-object git-gui.sh) &&
	git add git-gui.sh &&
	git commit -m "initial git-gui" &&
	cd .. &&
	mkdir git &&
	cd git &&
	git init &&
	echo git >git.c &&
	o2=$(git hash-object git.c) &&
	git add git.c &&
	git commit -m "initial git"
'

test_expect_success 'initial merge' '
	git remote add -f gui ../git-gui &&
	git merge -s ours --no-commit --allow-unrelated-histories gui/master &&
	git read-tree --prefix=git-gui/ -u gui/master &&
	git commit -m "Merge git-gui as our subdirectory" &&
	git checkout -b work &&
	git ls-files -s >actual &&
	(
		echo "100644 $o1 0	git-gui/git-gui.sh" &&
		echo "100644 $o2 0	git.c"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'merge update' '
	cd ../git-gui &&
	echo git-gui2 > git-gui.sh &&
	o3=$(git hash-object git-gui.sh) &&
	git add git-gui.sh &&
	git checkout -b master2 &&
	git commit -m "update git-gui" &&
	cd ../git &&
	git pull -s subtree gui master2 &&
	git ls-files -s >actual &&
	(
		echo "100644 $o3 0	git-gui/git-gui.sh" &&
		echo "100644 $o2 0	git.c"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'initial ambiguous subtree' '
	cd ../git &&
	git reset --hard master &&
	git checkout -b master2 &&
	git merge -s ours --no-commit gui/master &&
	git read-tree --prefix=git-gui2/ -u gui/master &&
	git commit -m "Merge git-gui2 as our subdirectory" &&
	git checkout -b work2 &&
	git ls-files -s >actual &&
	(
		echo "100644 $o1 0	git-gui/git-gui.sh" &&
		echo "100644 $o1 0	git-gui2/git-gui.sh" &&
		echo "100644 $o2 0	git.c"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'merge using explicit' '
	cd ../git &&
	git reset --hard master2 &&
	git pull -Xsubtree=git-gui gui master2 &&
	git ls-files -s >actual &&
	(
		echo "100644 $o3 0	git-gui/git-gui.sh" &&
		echo "100644 $o1 0	git-gui2/git-gui.sh" &&
		echo "100644 $o2 0	git.c"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'merge2 using explicit' '
	cd ../git &&
	git reset --hard master2 &&
	git pull -Xsubtree=git-gui2 gui master2 &&
	git ls-files -s >actual &&
	(
		echo "100644 $o1 0	git-gui/git-gui.sh" &&
		echo "100644 $o3 0	git-gui2/git-gui.sh" &&
		echo "100644 $o2 0	git.c"
	) >expected &&
	test_cmp expected actual
'

test_done
