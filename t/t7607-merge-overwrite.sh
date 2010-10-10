#!/bin/sh

test_description='git-merge

Do not overwrite changes.'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit c0 c0.c &&
	test_commit c1 c1.c &&
	test_commit c1a c1.c "c1 a" &&
	git reset --hard c0 &&
	test_commit c2 c2.c &&
	echo "VERY IMPORTANT CHANGES" > important
'

test_expect_success 'will not overwrite untracked file' '
	git reset --hard c1 &&
	cp important c2.c &&
	test_must_fail git merge c2 &&
	test_path_is_missing .git/MERGE_HEAD &&
	test_cmp important c2.c
'

test_expect_success 'will not overwrite new file' '
	git reset --hard c1 &&
	cp important c2.c &&
	git add c2.c &&
	test_must_fail git merge c2 &&
	test_path_is_missing .git/MERGE_HEAD &&
	test_cmp important c2.c
'

test_expect_success 'will not overwrite staged changes' '
	git reset --hard c1 &&
	cp important c2.c &&
	git add c2.c &&
	rm c2.c &&
	test_must_fail git merge c2 &&
	test_path_is_missing .git/MERGE_HEAD &&
	git checkout c2.c &&
	test_cmp important c2.c
'

test_expect_success 'will not overwrite removed file' '
	git reset --hard c1 &&
	git rm c1.c &&
	git commit -m "rm c1.c" &&
	cp important c1.c &&
	test_must_fail git merge c1a &&
	test_cmp important c1.c
'

test_expect_success 'will not overwrite re-added file' '
	git reset --hard c1 &&
	git rm c1.c &&
	git commit -m "rm c1.c" &&
	cp important c1.c &&
	git add c1.c &&
	test_must_fail git merge c1a &&
	test_path_is_missing .git/MERGE_HEAD &&
	test_cmp important c1.c
'

test_expect_success 'will not overwrite removed file with staged changes' '
	git reset --hard c1 &&
	git rm c1.c &&
	git commit -m "rm c1.c" &&
	cp important c1.c &&
	git add c1.c &&
	rm c1.c &&
	test_must_fail git merge c1a &&
	test_path_is_missing .git/MERGE_HEAD &&
	git checkout c1.c &&
	test_cmp important c1.c
'

cat >expect <<\EOF
error: Untracked working tree file 'c0.c' would be overwritten by merge.
fatal: read-tree failed
EOF

test_expect_success 'will not overwrite untracked file on unborn branch' '
	git reset --hard c0 &&
	git rm -fr . &&
	git checkout --orphan new &&
	cp important c0.c &&
	test_must_fail git merge c0 2>out &&
	test_cmp out expect &&
	test_path_is_missing .git/MERGE_HEAD &&
	test_cmp important c0.c
'

test_done
