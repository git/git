#!/bin/sh

test_description="merges with unrelated index changes"

. ./test-lib.sh

# Testcase for some simple merges
#   A
#   o-------o B
#    \
#     \-----o C
#      \
#       \---o D
#        \
#         \-o E
#          \
#           o F
#   cummit A: some file a
#   cummit B: adds file b, modifies end of a
#   cummit C: adds file c
#   cummit D: adds file d, modifies beginning of a
#   cummit E: renames a->subdir/a, adds subdir/e
#   cummit F: empty cummit

test_expect_success 'setup trivial merges' '
	test_seq 1 10 >a &&
	but add a &&
	test_tick && but cummit -m A &&

	but branch A &&
	but branch B &&
	but branch C &&
	but branch D &&
	but branch E &&
	but branch F &&

	but checkout B &&
	echo b >b &&
	echo 11 >>a &&
	but add a b &&
	test_tick && but cummit -m B &&

	but checkout C &&
	echo c >c &&
	but add c &&
	test_tick && but cummit -m C &&

	but checkout D &&
	test_seq 2 10 >a &&
	echo d >d &&
	but add a d &&
	test_tick && but cummit -m D &&

	but checkout E &&
	mkdir subdir &&
	but mv a subdir/a &&
	echo e >subdir/e &&
	but add subdir &&
	test_tick && but cummit -m E &&

	but checkout F &&
	test_tick && but cummit --allow-empty -m F
'

test_expect_success 'ff update' '
	but reset --hard &&
	but checkout A^0 &&

	touch random_file && but add random_file &&

	but merge E^0 &&

	test_must_fail but rev-parse HEAD:random_file &&
	test "$(but diff --name-only --cached E)" = "random_file"
'

test_expect_success 'ff update, important file modified' '
	but reset --hard &&
	but checkout A^0 &&

	mkdir subdir &&
	touch subdir/e &&
	but add subdir/e &&

	test_must_fail but merge E^0 &&
	test_path_is_missing .but/MERGE_HEAD
'

test_expect_success 'resolve, trivial' '
	but reset --hard &&
	but checkout B^0 &&

	touch random_file && but add random_file &&

	test_must_fail but merge -s resolve C^0 &&
	test_path_is_missing .but/MERGE_HEAD
'

test_expect_success 'resolve, non-trivial' '
	but reset --hard &&
	but checkout B^0 &&

	touch random_file && but add random_file &&

	test_must_fail but merge -s resolve D^0 &&
	test_path_is_missing .but/MERGE_HEAD
'

test_expect_success 'recursive' '
	but reset --hard &&
	but checkout B^0 &&

	touch random_file && but add random_file &&

	test_must_fail but merge -s recursive C^0 &&
	test_path_is_missing .but/MERGE_HEAD
'

test_expect_success 'recursive, when merge branch matches merge base' '
	but reset --hard &&
	but checkout B^0 &&

	touch random_file && but add random_file &&

	test_must_fail but merge -s recursive F^0 &&
	test_path_is_missing .but/MERGE_HEAD
'

test_expect_success 'merge-recursive, when index==head but head!=HEAD' '
	but reset --hard &&
	but checkout C^0 &&

	# Make index match B
	but diff C B -- | but apply --cached &&
	test_when_finished "but clean -fd" &&  # Do not leave untracked around
	# Merge B & F, with B as "head"
	but merge-recursive A -- B F > out &&
	test_i18ngrep "Already up to date" out
'

test_expect_success 'recursive, when file has staged changes not matching HEAD nor what a merge would give' '
	but reset --hard &&
	but checkout B^0 &&

	mkdir subdir &&
	test_seq 1 10 >subdir/a &&
	but add subdir/a &&

	# We have staged changes; merge should error out
	test_must_fail but merge -s recursive E^0 2>err &&
	test_i18ngrep "changes to the following files would be overwritten" err
'

test_expect_success 'recursive, when file has staged changes matching what a merge would give' '
	but reset --hard &&
	but checkout B^0 &&

	mkdir subdir &&
	test_seq 1 11 >subdir/a &&
	but add subdir/a &&

	# We have staged changes; merge should error out
	test_must_fail but merge -s recursive E^0 2>err &&
	test_i18ngrep "changes to the following files would be overwritten" err
'

test_expect_success 'octopus, unrelated file touched' '
	but reset --hard &&
	but checkout B^0 &&

	touch random_file && but add random_file &&

	test_must_fail but merge C^0 D^0 &&
	test_path_is_missing .but/MERGE_HEAD
'

test_expect_success 'octopus, related file removed' '
	but reset --hard &&
	but checkout B^0 &&

	but rm b &&

	test_must_fail but merge C^0 D^0 &&
	test_path_is_missing .but/MERGE_HEAD
'

test_expect_success 'octopus, related file modified' '
	but reset --hard &&
	but checkout B^0 &&

	echo 12 >>a && but add a &&

	test_must_fail but merge C^0 D^0 &&
	test_path_is_missing .but/MERGE_HEAD
'

test_expect_success 'ours' '
	but reset --hard &&
	but checkout B^0 &&

	touch random_file && but add random_file &&

	test_must_fail but merge -s ours C^0 &&
	test_path_is_missing .but/MERGE_HEAD
'

test_expect_success 'subtree' '
	but reset --hard &&
	but checkout B^0 &&

	touch random_file && but add random_file &&

	test_must_fail but merge -s subtree E^0 &&
	test_path_is_missing .but/MERGE_HEAD
'

test_done
