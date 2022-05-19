#!/bin/sh

test_description='subtree merge strategy'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '

	s="1 2 3 4 5 6 7 8" &&
	test_write_lines $s >hello &&
	but add hello &&
	but cummit -m initial &&
	but checkout -b side &&
	echo >>hello world &&
	but add hello &&
	but cummit -m second &&
	but checkout main &&
	test_write_lines mundo $s >hello &&
	but add hello &&
	but cummit -m main

'

test_expect_success 'subtree available and works like recursive' '

	but merge -s subtree side &&
	test_write_lines mundo $s world >expect &&
	test_cmp expect hello

'

test_expect_success 'setup branch sub' '
	but checkout --orphan sub &&
	but rm -rf . &&
	test_cummit foo
'

test_expect_success 'setup topic branch' '
	but checkout -b topic main &&
	but merge -s ours --no-cummit --allow-unrelated-histories sub &&
	but read-tree --prefix=dir/ -u sub &&
	but cummit -m "initial merge of sub into topic" &&
	test_path_is_file dir/foo.t &&
	test_path_is_file hello
'

test_expect_success 'update branch sub' '
	but checkout sub &&
	test_cummit bar
'

test_expect_success 'update topic branch' '
	but checkout topic &&
	but merge -s subtree sub -m "second merge of sub into topic" &&
	test_path_is_file dir/bar.t &&
	test_path_is_file dir/foo.t &&
	test_path_is_file hello
'

test_expect_success 'setup' '
	mkdir but-gui &&
	cd but-gui &&
	but init &&
	echo but-gui > but-gui.sh &&
	o1=$(but hash-object but-gui.sh) &&
	but add but-gui.sh &&
	but cummit -m "initial but-gui" &&
	cd .. &&
	mkdir but &&
	cd but &&
	but init &&
	echo but >but.c &&
	o2=$(but hash-object but.c) &&
	but add but.c &&
	but cummit -m "initial but"
'

test_expect_success 'initial merge' '
	but remote add -f gui ../but-gui &&
	but merge -s ours --no-cummit --allow-unrelated-histories gui/main &&
	but read-tree --prefix=but-gui/ -u gui/main &&
	but cummit -m "Merge but-gui as our subdirectory" &&
	but checkout -b work &&
	but ls-files -s >actual &&
	(
		echo "100644 $o1 0	but-gui/but-gui.sh" &&
		echo "100644 $o2 0	but.c"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'merge update' '
	cd ../but-gui &&
	echo but-gui2 > but-gui.sh &&
	o3=$(but hash-object but-gui.sh) &&
	but add but-gui.sh &&
	but checkout -b topic_2 &&
	but cummit -m "update but-gui" &&
	cd ../but &&
	but pull --no-rebase -s subtree gui topic_2 &&
	but ls-files -s >actual &&
	(
		echo "100644 $o3 0	but-gui/but-gui.sh" &&
		echo "100644 $o2 0	but.c"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'initial ambiguous subtree' '
	cd ../but &&
	but reset --hard main &&
	but checkout -b topic_2 &&
	but merge -s ours --no-cummit gui/main &&
	but read-tree --prefix=but-gui2/ -u gui/main &&
	but cummit -m "Merge but-gui2 as our subdirectory" &&
	but checkout -b work2 &&
	but ls-files -s >actual &&
	(
		echo "100644 $o1 0	but-gui/but-gui.sh" &&
		echo "100644 $o1 0	but-gui2/but-gui.sh" &&
		echo "100644 $o2 0	but.c"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'merge using explicit' '
	cd ../but &&
	but reset --hard topic_2 &&
	but pull --no-rebase -Xsubtree=but-gui gui topic_2 &&
	but ls-files -s >actual &&
	(
		echo "100644 $o3 0	but-gui/but-gui.sh" &&
		echo "100644 $o1 0	but-gui2/but-gui.sh" &&
		echo "100644 $o2 0	but.c"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'merge2 using explicit' '
	cd ../but &&
	but reset --hard topic_2 &&
	but pull --no-rebase -Xsubtree=but-gui2 gui topic_2 &&
	but ls-files -s >actual &&
	(
		echo "100644 $o1 0	but-gui/but-gui.sh" &&
		echo "100644 $o3 0	but-gui2/but-gui.sh" &&
		echo "100644 $o2 0	but.c"
	) >expected &&
	test_cmp expected actual
'

test_done
