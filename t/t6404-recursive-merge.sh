#!/bin/sh

test_description='Test merge without common ancestors'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# This scenario is based on a real-world repository of Shawn Pearce.

# 1 - A - D - F
#   \   X   /
#     B   X
#       X   \
# 2 - C - E - G

GIT_COMMITTER_DATE="2006-12-12 23:28:00 +0100"
export GIT_COMMITTER_DATE

test_expect_success 'setup tests' '
	GIT_TEST_COMMIT_GRAPH=0 &&
	export GIT_TEST_COMMIT_GRAPH &&
	echo 1 >a1 &&
	git add a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:00" git commit -m 1 a1 &&

	git checkout -b A main &&
	echo A >a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:01" git commit -m A a1 &&

	git checkout -b B main &&
	echo B >a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:02" git commit -m B a1 &&

	git checkout -b D A &&
	git rev-parse B >.git/MERGE_HEAD &&
	echo D >a1 &&
	git update-index a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:03" git commit -m D &&

	git symbolic-ref HEAD refs/heads/other &&
	echo 2 >a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:04" git commit -m 2 a1 &&

	git checkout -b C &&
	echo C >a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:05" git commit -m C a1 &&

	git checkout -b E C &&
	git rev-parse B >.git/MERGE_HEAD &&
	echo E >a1 &&
	git update-index a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:06" git commit -m E &&

	git checkout -b G E &&
	git rev-parse A >.git/MERGE_HEAD &&
	echo G >a1 &&
	git update-index a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:07" git commit -m G &&

	git checkout -b F D &&
	git rev-parse C >.git/MERGE_HEAD &&
	echo F >a1 &&
	git update-index a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:08" git commit -m F &&

	test_oid_cache <<-EOF
	idxstage1 sha1:ec3fe2a791706733f2d8fa7ad45d9a9672031f5e
	idxstage1 sha256:b3c8488929903aaebdeb22270cb6d36e5b8724b01ae0d4da24632f158c99676f
	EOF
'

test_expect_success 'combined merge conflicts' '
	test_must_fail git merge -m final G
'

test_expect_success 'result contains a conflict' '
	cat >expect <<-\EOF &&
	<<<<<<< HEAD
	F
	=======
	G
	>>>>>>> G
	EOF

	test_cmp expect a1
'

test_expect_success 'virtual trees were processed' '
	# TODO: fragile test, relies on ambigious merge-base resolution
	git ls-files --stage >out &&

	cat >expect <<-EOF &&
	100644 $(test_oid idxstage1) 1	a1
	100644 $(git rev-parse F:a1) 2	a1
	100644 $(git rev-parse G:a1) 3	a1
	EOF

	test_cmp expect out
'

test_expect_success 'refuse to merge binary files' '
	git reset --hard &&
	printf "\0" >binary-file &&
	git add binary-file &&
	git commit -m binary &&
	git checkout G &&
	printf "\0\0" >binary-file &&
	git add binary-file &&
	git commit -m binary2 &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		test_must_fail git merge F >merge_output
	else
		test_must_fail git merge F 2>merge_output
	fi &&
	grep "Cannot merge binary files: binary-file (HEAD vs. F)" merge_output
'

test_expect_success 'mark rename/delete as unmerged' '

	git reset --hard &&
	git checkout -b delete &&
	git rm a1 &&
	test_tick &&
	git commit -m delete &&
	git checkout -b rename HEAD^ &&
	git mv a1 a2 &&
	test_tick &&
	git commit -m rename &&
	test_must_fail git merge delete &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		test 2 = $(git ls-files --unmerged | wc -l)
	else
		test 1 = $(git ls-files --unmerged | wc -l)
	fi &&
	git rev-parse --verify :2:a2 &&
	test_must_fail git rev-parse --verify :3:a2 &&
	git checkout -f delete &&
	test_must_fail git merge rename &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		test 2 = $(git ls-files --unmerged | wc -l)
	else
		test 1 = $(git ls-files --unmerged | wc -l)
	fi &&
	test_must_fail git rev-parse --verify :2:a2 &&
	git rev-parse --verify :3:a2
'

test_done
