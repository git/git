#!/bin/sh

test_description="merges with unrelated index changes"

. ./test-lib.sh

# Testcase for some simple merges
#   A
#   o-----o B
#    \
#     \---o C
#      \
#       \-o D
#        \
#         o E
#   Commit A: some file a
#   Commit B: adds file b, modifies end of a
#   Commit C: adds file c
#   Commit D: adds file d, modifies beginning of a
#   Commit E: renames a->subdir/a, adds subdir/e

test_expect_success 'setup trivial merges' '
	test_seq 1 10 >a &&
	git add a &&
	test_tick && git commit -m A &&

	git branch A &&
	git branch B &&
	git branch C &&
	git branch D &&
	git branch E &&

	git checkout B &&
	echo b >b &&
	echo 11 >>a &&
	git add a b &&
	test_tick && git commit -m B &&

	git checkout C &&
	echo c >c &&
	git add c &&
	test_tick && git commit -m C &&

	git checkout D &&
	test_seq 2 10 >a &&
	echo d >d &&
	git add a d &&
	test_tick && git commit -m D &&

	git checkout E &&
	mkdir subdir &&
	git mv a subdir/a &&
	echo e >subdir/e &&
	git add subdir &&
	test_tick && git commit -m E
'

test_expect_success 'ff update' '
	git reset --hard &&
	git checkout A^0 &&

	touch random_file && git add random_file &&

	git merge E^0 &&

	test_must_fail git rev-parse HEAD:random_file &&
	test "$(git diff --name-only --cached E)" = "random_file"
'

test_expect_success 'ff update, important file modified' '
	git reset --hard &&
	git checkout A^0 &&

	mkdir subdir &&
	touch subdir/e &&
	git add subdir/e &&

	test_must_fail git merge E^0
'

test_expect_success 'resolve, trivial' '
	git reset --hard &&
	git checkout B^0 &&

	touch random_file && git add random_file &&

	test_must_fail git merge -s resolve C^0
'

test_expect_success 'resolve, non-trivial' '
	git reset --hard &&
	git checkout B^0 &&

	touch random_file && git add random_file &&

	test_must_fail git merge -s resolve D^0
'

test_expect_success 'recursive' '
	git reset --hard &&
	git checkout B^0 &&

	touch random_file && git add random_file &&

	test_must_fail git merge -s recursive C^0
'

test_expect_success 'octopus, unrelated file touched' '
	git reset --hard &&
	git checkout B^0 &&

	touch random_file && git add random_file &&

	test_must_fail git merge C^0 D^0
'

test_expect_success 'octopus, related file removed' '
	git reset --hard &&
	git checkout B^0 &&

	git rm b &&

	test_must_fail git merge C^0 D^0
'

test_expect_success 'octopus, related file modified' '
	git reset --hard &&
	git checkout B^0 &&

	echo 12 >>a && git add a &&

	test_must_fail git merge C^0 D^0
'

test_expect_success 'ours' '
	git reset --hard &&
	git checkout B^0 &&

	touch random_file && git add random_file &&

	test_must_fail git merge -s ours C^0
'

test_expect_success 'subtree' '
	git reset --hard &&
	git checkout B^0 &&

	touch random_file && git add random_file &&

	test_must_fail git merge -s subtree E^0
'

test_done
