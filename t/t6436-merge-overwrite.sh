#!/bin/sh

test_description='but-merge

Do not overwrite changes.'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_cummit c0 c0.c &&
	test_cummit c1 c1.c &&
	test_cummit c1a c1.c "c1 a" &&
	but reset --hard c0 &&
	test_cummit c2 c2.c &&
	but reset --hard c0 &&
	mkdir sub &&
	echo "sub/f" > sub/f &&
	mkdir sub2 &&
	echo "sub2/f" > sub2/f &&
	but add sub/f sub2/f &&
	but cummit -m sub &&
	but tag sub &&
	echo "VERY IMPORTANT CHANGES" > important
'

test_expect_success 'will not overwrite untracked file' '
	but reset --hard c1 &&
	cp important c2.c &&
	test_must_fail but merge c2 &&
	test_path_is_missing .but/MERGE_HEAD &&
	test_cmp important c2.c
'

test_expect_success 'will overwrite tracked file' '
	but reset --hard c1 &&
	cp important c2.c &&
	but add c2.c &&
	but cummit -m important &&
	but checkout c2
'

test_expect_success 'will not overwrite new file' '
	but reset --hard c1 &&
	cp important c2.c &&
	but add c2.c &&
	test_must_fail but merge c2 &&
	test_path_is_missing .but/MERGE_HEAD &&
	test_cmp important c2.c
'

test_expect_success 'will not overwrite staged changes' '
	but reset --hard c1 &&
	cp important c2.c &&
	but add c2.c &&
	rm c2.c &&
	test_must_fail but merge c2 &&
	test_path_is_missing .but/MERGE_HEAD &&
	but checkout c2.c &&
	test_cmp important c2.c
'

test_expect_success 'will not overwrite removed file' '
	but reset --hard c1 &&
	but rm c1.c &&
	but cummit -m "rm c1.c" &&
	cp important c1.c &&
	test_must_fail but merge c1a &&
	test_cmp important c1.c &&
	rm c1.c  # Do not leave untracked file in way of future tests
'

test_expect_success 'will not overwrite re-added file' '
	but reset --hard c1 &&
	but rm c1.c &&
	but cummit -m "rm c1.c" &&
	cp important c1.c &&
	but add c1.c &&
	test_must_fail but merge c1a &&
	test_path_is_missing .but/MERGE_HEAD &&
	test_cmp important c1.c
'

test_expect_success 'will not overwrite removed file with staged changes' '
	but reset --hard c1 &&
	but rm c1.c &&
	but cummit -m "rm c1.c" &&
	cp important c1.c &&
	but add c1.c &&
	rm c1.c &&
	test_must_fail but merge c1a &&
	test_path_is_missing .but/MERGE_HEAD &&
	but checkout c1.c &&
	test_cmp important c1.c
'

test_expect_success 'will not overwrite unstaged changes in renamed file' '
	but reset --hard c1 &&
	but mv c1.c other.c &&
	but cummit -m rename &&
	cp important other.c &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		test_must_fail but merge c1a >out 2>err &&
		test_i18ngrep "would be overwritten by merge" err &&
		test_cmp important other.c &&
		test_path_is_missing .but/MERGE_HEAD
	else
		test_must_fail but merge c1a >out &&
		test_i18ngrep "Refusing to lose dirty file at other.c" out &&
		test_path_is_file other.c~HEAD &&
		test $(but hash-object other.c~HEAD) = $(but rev-parse c1a:c1.c) &&
		test_cmp important other.c
	fi
'

test_expect_success 'will not overwrite untracked subtree' '
	but reset --hard c0 &&
	rm -rf sub &&
	mkdir -p sub/f &&
	cp important sub/f/important &&
	test_must_fail but merge sub &&
	test_path_is_missing .but/MERGE_HEAD &&
	test_cmp important sub/f/important
'

cat >expect <<\EOF
error: The following untracked working tree files would be overwritten by merge:
	sub
	sub2
Please move or remove them before you merge.
Aborting
EOF

test_expect_success 'will not overwrite untracked file in leading path' '
	but reset --hard c0 &&
	rm -rf sub &&
	cp important sub &&
	cp important sub2 &&
	test_must_fail but merge sub 2>out &&
	test_cmp out expect &&
	test_path_is_missing .but/MERGE_HEAD &&
	test_cmp important sub &&
	test_cmp important sub2 &&
	rm -f sub sub2
'

test_expect_success SYMLINKS 'will not overwrite untracked symlink in leading path' '
	but reset --hard c0 &&
	rm -rf sub &&
	mkdir sub2 &&
	ln -s sub2 sub &&
	test_must_fail but merge sub &&
	test_path_is_missing .but/MERGE_HEAD
'

test_expect_success 'will not be confused by symlink in leading path' '
	but reset --hard c0 &&
	rm -rf sub &&
	test_ln_s_add sub2 sub &&
	but cummit -m ln &&
	but checkout sub
'

cat >expect <<\EOF
error: Untracked working tree file 'c0.c' would be overwritten by merge.
fatal: read-tree failed
EOF

test_expect_success 'will not overwrite untracked file on unborn branch' '
	but reset --hard c0 &&
	but rm -fr . &&
	but checkout --orphan new &&
	cp important c0.c &&
	test_must_fail but merge c0 2>out &&
	test_cmp out expect
'

test_expect_success 'will not overwrite untracked file on unborn branch .but/MERGE_HEAD sanity etc.' '
	test_when_finished "rm c0.c" &&
	test_path_is_missing .but/MERGE_HEAD &&
	test_cmp important c0.c
'

test_expect_success 'failed merge leaves unborn branch in the womb' '
	test_must_fail but rev-parse --verify HEAD
'

test_expect_success 'set up unborn branch and content' '
	but symbolic-ref HEAD refs/heads/unborn &&
	rm -f .but/index &&
	echo foo > tracked-file &&
	but add tracked-file &&
	echo bar > untracked-file
'

test_expect_success 'will not clobber WT/index when merging into unborn' '
	but merge main &&
	grep foo tracked-file &&
	but show :tracked-file >expect &&
	grep foo expect &&
	grep bar untracked-file
'

test_done
