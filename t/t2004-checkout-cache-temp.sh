#!/bin/sh
#
# Copyright (c) 2006 Shawn Pearce
#

test_description='git checkout-index --temp test.

With --temp flag, git checkout-index writes to temporary merge files
rather than the tracked path.'

. ./test-lib.sh

test_expect_success 'setup' '
	mkdir asubdir &&
	echo tree1path0 >path0 &&
	echo tree1path1 >path1 &&
	echo tree1path3 >path3 &&
	echo tree1path4 >path4 &&
	echo tree1asubdir/path5 >asubdir/path5 &&
	git update-index --add path0 path1 path3 path4 asubdir/path5 &&
	t1=$(git write-tree) &&
	rm -f path* .merge_* actual .git/index &&
	echo tree2path0 >path0 &&
	echo tree2path1 >path1 &&
	echo tree2path2 >path2 &&
	echo tree2path4 >path4 &&
	git update-index --add path0 path1 path2 path4 &&
	t2=$(git write-tree) &&
	rm -f path* .merge_* actual .git/index &&
	echo tree2path0 >path0 &&
	echo tree3path1 >path1 &&
	echo tree3path2 >path2 &&
	echo tree3path3 >path3 &&
	git update-index --add path0 path1 path2 path3 &&
	t3=$(git write-tree)
'

test_expect_success 'checkout one stage 0 to temporary file' '
	rm -f path* .merge_* actual .git/index &&
	git read-tree $t1 &&
	git checkout-index --temp -- path1 >actual &&
	test_line_count = 1 actual &&
	test $(cut "-d	" -f2 actual) = path1 &&
	p=$(cut "-d	" -f1 actual) &&
	test -f $p &&
	test $(cat $p) = tree1path1
'

test_expect_success 'checkout all stage 0 to temporary files' '
	rm -f path* .merge_* actual .git/index &&
	git read-tree $t1 &&
	git checkout-index -a --temp >actual &&
	test_line_count = 5 actual &&
	for f in path0 path1 path3 path4 asubdir/path5
	do
		test $(grep $f actual | cut "-d	" -f2) = $f &&
		p=$(grep $f actual | cut "-d	" -f1) &&
		test -f $p &&
		test $(cat $p) = tree1$f || return 1
	done
'

test_expect_success 'setup 3-way merge' '
	rm -f path* .merge_* actual .git/index &&
	git read-tree -m $t1 $t2 $t3
'

test_expect_success 'checkout one stage 2 to temporary file' '
	rm -f path* .merge_* actual &&
	git checkout-index --stage=2 --temp -- path1 >actual &&
	test_line_count = 1 actual &&
	test $(cut "-d	" -f2 actual) = path1 &&
	p=$(cut "-d	" -f1 actual) &&
	test -f $p &&
	test $(cat $p) = tree2path1
'

test_expect_success 'checkout all stage 2 to temporary files' '
	rm -f path* .merge_* actual &&
	git checkout-index --all --stage=2 --temp >actual &&
	test_line_count = 3 actual &&
	for f in path1 path2 path4
	do
		test $(grep $f actual | cut "-d	" -f2) = $f &&
		p=$(grep $f actual | cut "-d	" -f1) &&
		test -f $p &&
		test $(cat $p) = tree2$f || return 1
	done
'

test_expect_success 'checkout all stages of unknown path' '
	rm -f path* .merge_* actual &&
	test_must_fail git checkout-index --stage=all --temp \
		-- does-not-exist 2>stderr &&
	test_grep not.in.the.cache stderr
'

test_expect_success 'checkout all stages/one file to nothing' '
	rm -f path* .merge_* actual &&
	git checkout-index --stage=all --temp -- path0 >actual 2>stderr &&
	test_must_be_empty stderr &&
	test_line_count = 0 actual
'

test_expect_success 'checkout all stages/one file to temporary files' '
	rm -f path* .merge_* actual &&
	git checkout-index --stage=all --temp -- path1 >actual &&
	test_line_count = 1 actual &&
	test $(cut "-d	" -f2 actual) = path1 &&
	cut "-d	" -f1 actual | (read s1 s2 s3 &&
	test -f $s1 &&
	test -f $s2 &&
	test -f $s3 &&
	test $(cat $s1) = tree1path1 &&
	test $(cat $s2) = tree2path1 &&
	test $(cat $s3) = tree3path1)
'

test_expect_success '--stage=all implies --temp' '
	rm -f path* .merge_* actual &&
	git checkout-index --stage=all -- path1 &&
	test_path_is_missing path1
'

test_expect_success 'overriding --stage=all resets implied --temp' '
	rm -f path* .merge_* actual &&
	git checkout-index --stage=all --stage=2 -- path1 &&
	echo tree2path1 >expect &&
	test_cmp expect path1
'

test_expect_success '--stage=all --no-temp is rejected' '
	rm -f path* .merge_* actual &&
	test_must_fail git checkout-index --stage=all --no-temp -- path1 2>err &&
	grep -v "already exists" err &&
	grep "options .--stage=all. and .--no-temp. cannot be used together" err
'

test_expect_success 'checkout some stages/one file to temporary files' '
	rm -f path* .merge_* actual &&
	git checkout-index --stage=all --temp -- path2 >actual &&
	test_line_count = 1 actual &&
	test $(cut "-d	" -f2 actual) = path2 &&
	cut "-d	" -f1 actual | (read s1 s2 s3 &&
	test $s1 = . &&
	test -f $s2 &&
	test -f $s3 &&
	test $(cat $s2) = tree2path2 &&
	test $(cat $s3) = tree3path2)
'

test_expect_success 'checkout all stages/all files to temporary files' '
	rm -f path* .merge_* actual &&
	git checkout-index -a --stage=all --temp >actual &&
	test_line_count = 5 actual
'

test_expect_success '-- path0: no entry' '
	test x$(grep path0 actual | cut "-d	" -f2) = x
'

test_expect_success '-- path1: all 3 stages' '
	test $(grep path1 actual | cut "-d	" -f2) = path1 &&
	grep path1 actual | cut "-d	" -f1 | (read s1 s2 s3 &&
	test -f $s1 &&
	test -f $s2 &&
	test -f $s3 &&
	test $(cat $s1) = tree1path1 &&
	test $(cat $s2) = tree2path1 &&
	test $(cat $s3) = tree3path1)
'

test_expect_success '-- path2: no stage 1, have stage 2 and 3' '
	test $(grep path2 actual | cut "-d	" -f2) = path2 &&
	grep path2 actual | cut "-d	" -f1 | (read s1 s2 s3 &&
	test $s1 = . &&
	test -f $s2 &&
	test -f $s3 &&
	test $(cat $s2) = tree2path2 &&
	test $(cat $s3) = tree3path2)
'

test_expect_success '-- path3: no stage 2, have stage 1 and 3' '
	test $(grep path3 actual | cut "-d	" -f2) = path3 &&
	grep path3 actual | cut "-d	" -f1 | (read s1 s2 s3 &&
	test -f $s1 &&
	test $s2 = . &&
	test -f $s3 &&
	test $(cat $s1) = tree1path3 &&
	test $(cat $s3) = tree3path3)
'

test_expect_success '-- path4: no stage 3, have stage 1 and 3' '
	test $(grep path4 actual | cut "-d	" -f2) = path4 &&
	grep path4 actual | cut "-d	" -f1 | (read s1 s2 s3 &&
	test -f $s1 &&
	test -f $s2 &&
	test $s3 = . &&
	test $(cat $s1) = tree1path4 &&
	test $(cat $s2) = tree2path4)
'

test_expect_success '-- asubdir/path5: no stage 2 and 3 have stage 1' '
	test $(grep asubdir/path5 actual | cut "-d	" -f2) = asubdir/path5 &&
	grep asubdir/path5 actual | cut "-d	" -f1 | (read s1 s2 s3 &&
	test -f $s1 &&
	test $s2 = . &&
	test $s3 = . &&
	test $(cat $s1) = tree1asubdir/path5)
'

test_expect_success 'checkout --temp within subdir' '
	(
		cd asubdir &&
		git checkout-index -a --stage=all >actual &&
		test_line_count = 1 actual &&
		test $(grep path5 actual | cut "-d	" -f2) = path5 &&
		grep path5 actual | cut "-d	" -f1 | (read s1 s2 s3 &&
		test -f ../$s1 &&
		test $s2 = . &&
		test $s3 = . &&
		test $(cat ../$s1) = tree1asubdir/path5)
	)
'

test_expect_success 'checkout --temp symlink' '
	rm -f path* .merge_* actual .git/index &&
	test_ln_s_add path7 path6 &&
	git checkout-index --temp -a >actual &&
	test_line_count = 1 actual &&
	test $(cut "-d	" -f2 actual) = path6 &&
	p=$(cut "-d	" -f1 actual) &&
	test -f $p &&
	test $(cat $p) = path7
'

test_expect_success 'emit well-formed relative path' '
	rm -f path* .merge_* actual .git/index &&
	>path0123456789 &&
	git update-index --add path0123456789 &&
	(
		cd asubdir &&
		git checkout-index --temp -- ../path0123456789 >actual &&
		test_line_count = 1 actual &&
		test $(cut "-d	" -f2 actual) = ../path0123456789
	)
'

test_done
