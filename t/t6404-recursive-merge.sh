#!/bin/sh

test_description='Test merge without common ancestors'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# This scenario is based on a real-world repository of Shawn Pearce.

# 1 - A - D - F
#   \   X   /
#     B   X
#       X   \
# 2 - C - E - G

GIT_CUMMITTER_DATE="2006-12-12 23:28:00 +0100"
export GIT_CUMMITTER_DATE

test_expect_success 'setup tests' '
	GIT_TEST_CUMMIT_GRAPH=0 &&
	export GIT_TEST_CUMMIT_GRAPH &&
	echo 1 >a1 &&
	but add a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:00" but cummit -m 1 a1 &&

	but checkout -b A main &&
	echo A >a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:01" but cummit -m A a1 &&

	but checkout -b B main &&
	echo B >a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:02" but cummit -m B a1 &&

	but checkout -b D A &&
	but rev-parse B >.but/MERGE_HEAD &&
	echo D >a1 &&
	but update-index a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:03" but cummit -m D &&

	but symbolic-ref HEAD refs/heads/other &&
	echo 2 >a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:04" but cummit -m 2 a1 &&

	but checkout -b C &&
	echo C >a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:05" but cummit -m C a1 &&

	but checkout -b E C &&
	but rev-parse B >.but/MERGE_HEAD &&
	echo E >a1 &&
	but update-index a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:06" but cummit -m E &&

	but checkout -b G E &&
	but rev-parse A >.but/MERGE_HEAD &&
	echo G >a1 &&
	but update-index a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:07" but cummit -m G &&

	but checkout -b F D &&
	but rev-parse C >.but/MERGE_HEAD &&
	echo F >a1 &&
	but update-index a1 &&
	GIT_AUTHOR_DATE="2006-12-12 23:00:08" but cummit -m F &&

	test_oid_cache <<-EOF
	idxstage1 sha1:ec3fe2a791706733f2d8fa7ad45d9a9672031f5e
	idxstage1 sha256:b3c8488929903aaebdeb22270cb6d36e5b8724b01ae0d4da24632f158c99676f
	EOF
'

test_expect_success 'combined merge conflicts' '
	test_must_fail but merge -m final G
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
	but ls-files --stage >out &&

	cat >expect <<-EOF &&
	100644 $(test_oid idxstage1) 1	a1
	100644 $(but rev-parse F:a1) 2	a1
	100644 $(but rev-parse G:a1) 3	a1
	EOF

	test_cmp expect out
'

test_expect_success 'refuse to merge binary files' '
	but reset --hard &&
	printf "\0" >binary-file &&
	but add binary-file &&
	but cummit -m binary &&
	but checkout G &&
	printf "\0\0" >binary-file &&
	but add binary-file &&
	but cummit -m binary2 &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		test_must_fail but merge F >merge_output
	else
		test_must_fail but merge F 2>merge_output
	fi &&
	grep "Cannot merge binary files: binary-file (HEAD vs. F)" merge_output
'

test_expect_success 'mark rename/delete as unmerged' '

	but reset --hard &&
	but checkout -b delete &&
	but rm a1 &&
	test_tick &&
	but cummit -m delete &&
	but checkout -b rename HEAD^ &&
	but mv a1 a2 &&
	test_tick &&
	but cummit -m rename &&
	test_must_fail but merge delete &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		test 2 = $(but ls-files --unmerged | wc -l)
	else
		test 1 = $(but ls-files --unmerged | wc -l)
	fi &&
	but rev-parse --verify :2:a2 &&
	test_must_fail but rev-parse --verify :3:a2 &&
	but checkout -f delete &&
	test_must_fail but merge rename &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		test 2 = $(but ls-files --unmerged | wc -l)
	else
		test 1 = $(but ls-files --unmerged | wc -l)
	fi &&
	test_must_fail but rev-parse --verify :2:a2 &&
	but rev-parse --verify :3:a2
'

test_done
