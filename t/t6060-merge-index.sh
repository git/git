#!/bin/sh

test_description='basic git merge-index / git-merge-one-file tests'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup diverging branches' '
	test_write_lines 1 2 3 4 5 6 7 8 9 10 >file &&
	git add file &&
	git commit -m base &&
	git tag base &&
	sed s/2/two/ <file >tmp &&
	mv tmp file &&
	git commit -a -m two &&
	git tag two &&
	git checkout -b other HEAD^ &&
	sed s/10/ten/ <file >tmp &&
	mv tmp file &&
	git commit -a -m ten &&
	git tag ten
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
	git read-tree -i -m base ten two &&
	echo file >expect &&
	git diff-files --name-only --diff-filter=U >unmerged &&
	test_cmp expect unmerged
'

test_expect_success 'git merge-index git-merge-one-file resolves' '
	git merge-index git-merge-one-file -a &&
	git diff-files --name-only --diff-filter=U >unmerged &&
	test_must_be_empty unmerged &&
	test_cmp expect-merged file &&
	git cat-file blob :file >file-index &&
	test_cmp expect-merged file-index
'

test_expect_success 'setup bare merge' '
	git clone --bare . bare.git &&
	(cd bare.git &&
	 GIT_INDEX_FILE=$PWD/merge.index &&
	 export GIT_INDEX_FILE &&
	 git read-tree -i -m base ten two
	)
'

test_expect_success 'merge-one-file fails without a work tree' '
	(cd bare.git &&
	 GIT_INDEX_FILE=$PWD/merge.index &&
	 export GIT_INDEX_FILE &&
	 test_must_fail git merge-index git-merge-one-file -a
	)
'

test_expect_success 'merge-one-file respects GIT_WORK_TREE' '
	(cd bare.git &&
	 mkdir work &&
	 GIT_WORK_TREE=$PWD/work &&
	 export GIT_WORK_TREE &&
	 GIT_INDEX_FILE=$PWD/merge.index &&
	 export GIT_INDEX_FILE &&
	 git merge-index git-merge-one-file -a &&
	 git cat-file blob :file >work/file-index
	) &&
	test_cmp expect-merged bare.git/work/file &&
	test_cmp expect-merged bare.git/work/file-index
'

test_expect_success 'merge-one-file respects core.worktree' '
	mkdir subdir &&
	git clone . subdir/child &&
	(cd subdir &&
	 GIT_DIR=$PWD/child/.git &&
	 export GIT_DIR &&
	 git config core.worktree "$PWD/child" &&
	 git read-tree -i -m base ten two &&
	 git merge-index git-merge-one-file -a &&
	 git cat-file blob :file >file-index
	) &&
	test_cmp expect-merged subdir/child/file &&
	test_cmp expect-merged subdir/file-index
'

test_done
