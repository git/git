#!/bin/sh

test_description='basic but merge-index / but-merge-one-file tests'
. ./test-lib.sh

test_expect_success 'setup diverging branches' '
	test_write_lines 1 2 3 4 5 6 7 8 9 10 >file &&
	but add file &&
	but cummit -m base &&
	but tag base &&
	sed s/2/two/ <file >tmp &&
	mv tmp file &&
	but cummit -a -m two &&
	but tag two &&
	but checkout -b other HEAD^ &&
	sed s/10/ten/ <file >tmp &&
	mv tmp file &&
	but cummit -a -m ten &&
	but tag ten
'

cat >expect-merged <<'EOF'
1
two
3
4
5
6
7
8
9
ten
EOF

test_expect_success 'read-tree does not resolve content merge' '
	but read-tree -i -m base ten two &&
	echo file >expect &&
	but diff-files --name-only --diff-filter=U >unmerged &&
	test_cmp expect unmerged
'

test_expect_success 'but merge-index but-merge-one-file resolves' '
	but merge-index but-merge-one-file -a &&
	but diff-files --name-only --diff-filter=U >unmerged &&
	test_must_be_empty unmerged &&
	test_cmp expect-merged file &&
	but cat-file blob :file >file-index &&
	test_cmp expect-merged file-index
'

test_expect_success 'setup bare merge' '
	but clone --bare . bare.but &&
	(cd bare.but &&
	 BUT_INDEX_FILE=$PWD/merge.index &&
	 export BUT_INDEX_FILE &&
	 but read-tree -i -m base ten two
	)
'

test_expect_success 'merge-one-file fails without a work tree' '
	(cd bare.but &&
	 BUT_INDEX_FILE=$PWD/merge.index &&
	 export BUT_INDEX_FILE &&
	 test_must_fail but merge-index but-merge-one-file -a
	)
'

test_expect_success 'merge-one-file respects BUT_WORK_TREE' '
	(cd bare.but &&
	 mkdir work &&
	 BUT_WORK_TREE=$PWD/work &&
	 export BUT_WORK_TREE &&
	 BUT_INDEX_FILE=$PWD/merge.index &&
	 export BUT_INDEX_FILE &&
	 but merge-index but-merge-one-file -a &&
	 but cat-file blob :file >work/file-index
	) &&
	test_cmp expect-merged bare.but/work/file &&
	test_cmp expect-merged bare.but/work/file-index
'

test_expect_success 'merge-one-file respects core.worktree' '
	mkdir subdir &&
	but clone . subdir/child &&
	(cd subdir &&
	 BUT_DIR=$PWD/child/.but &&
	 export BUT_DIR &&
	 but config core.worktree "$PWD/child" &&
	 but read-tree -i -m base ten two &&
	 but merge-index but-merge-one-file -a &&
	 but cat-file blob :file >file-index
	) &&
	test_cmp expect-merged subdir/child/file &&
	test_cmp expect-merged subdir/file-index
'

test_done
