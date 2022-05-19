#!/bin/sh

test_description='merging with submodules'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

GIT_TEST_FATAL_REGISTER_SUBMODULE_ODB=1
export GIT_TEST_FATAL_REGISTER_SUBMODULE_ODB

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-merge.sh

#
# history
#
#        a --- c
#      /   \ /
# root      X
#      \   / \
#        b --- d
#

test_expect_success setup '

	mkdir sub &&
	(cd sub &&
	 but init &&
	 echo original > file &&
	 but add file &&
	 test_tick &&
	 but cummit -m sub-root) &&
	but add sub &&
	test_tick &&
	but cummit -m root &&

	but checkout -b a main &&
	(cd sub &&
	 echo A > file &&
	 but add file &&
	 test_tick &&
	 but cummit -m sub-a) &&
	but add sub &&
	test_tick &&
	but cummit -m a &&

	but checkout -b b main &&
	(cd sub &&
	 echo B > file &&
	 but add file &&
	 test_tick &&
	 but cummit -m sub-b) &&
	but add sub &&
	test_tick &&
	but cummit -m b &&

	but checkout -b c a &&
	but merge -s ours b &&

	but checkout -b d b &&
	but merge -s ours a
'

# History setup
#
#             b
#           /   \
#  init -- a     d
#    \      \   /
#     g       c
#
# a in the main repository records to sub-a in the submodule and
# analogous b and c. d should be automatically found by merging c into
# b in the main repository.
test_expect_success 'setup for merge search' '
	mkdir merge-search &&
	(cd merge-search &&
	but init &&
	mkdir sub &&
	(cd sub &&
	 but init &&
	 echo "file-a" > file-a &&
	 but add file-a &&
	 but cummit -m "sub-a" &&
	 but branch sub-a) &&
	but cummit --allow-empty -m init &&
	but branch init &&
	but add sub &&
	but cummit -m "a" &&
	but branch a &&

	but checkout -b b &&
	(cd sub &&
	 but checkout -b sub-b &&
	 echo "file-b" > file-b &&
	 but add file-b &&
	 but cummit -m "sub-b") &&
	but cummit -a -m "b" &&

	but checkout -b c a &&
	(cd sub &&
	 but checkout -b sub-c sub-a &&
	 echo "file-c" > file-c &&
	 but add file-c &&
	 but cummit -m "sub-c") &&
	but cummit -a -m "c" &&

	but checkout -b d a &&
	(cd sub &&
	 but checkout -b sub-d sub-b &&
	 but merge sub-c) &&
	but cummit -a -m "d" &&
	but branch test b &&

	but checkout -b g init &&
	(cd sub &&
	 but checkout -b sub-g sub-c) &&
	but add sub &&
	but cummit -a -m "g")
'

test_expect_success 'merge with one side as a fast-forward of the other' '
	(cd merge-search &&
	 but checkout -b test-forward b &&
	 but merge d &&
	 but ls-tree test-forward sub | cut -f1 | cut -f3 -d" " > actual &&
	 (cd sub &&
	  but rev-parse sub-d > ../expect) &&
	 test_cmp expect actual)
'

test_expect_success 'merging should conflict for non fast-forward' '
	(cd merge-search &&
	 but checkout -b test-nonforward b &&
	 (cd sub &&
	  but rev-parse sub-d > ../expect) &&
	  if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	  then
		test_must_fail but merge c >actual
	  else
		test_must_fail but merge c 2> actual
	  fi &&
	 grep $(cat expect) actual > /dev/null &&
	 but reset --hard)
'

test_expect_success 'merging should fail for ambiguous common parent' '
	(cd merge-search &&
	but checkout -b test-ambiguous b &&
	(cd sub &&
	 but checkout -b ambiguous sub-b &&
	 but merge sub-c &&
	 if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	 then
		but rev-parse --short sub-d >../expect1 &&
		but rev-parse --short ambiguous >../expect2
	 else
		but rev-parse sub-d > ../expect1 &&
		but rev-parse ambiguous > ../expect2
	 fi
	 ) &&
	 if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	 then
		test_must_fail but merge c >actual
	 else
		test_must_fail but merge c 2> actual
	 fi &&
	grep $(cat expect1) actual > /dev/null &&
	grep $(cat expect2) actual > /dev/null &&
	but reset --hard)
'

# in a situation like this
#
# submodule tree:
#
#    sub-a --- sub-b --- sub-d
#
# main tree:
#
#    e (sub-a)
#   /
#  bb (sub-b)
#   \
#    f (sub-d)
#
# A merge between e and f should fail because one of the submodule
# cummits (sub-a) does not descend from the submodule merge-base (sub-b).
#
test_expect_success 'merging should fail for changes that are backwards' '
	(cd merge-search &&
	but checkout -b bb a &&
	(cd sub &&
	 but checkout sub-b) &&
	but cummit -a -m "bb" &&

	but checkout -b e bb &&
	(cd sub &&
	 but checkout sub-a) &&
	but cummit -a -m "e" &&

	but checkout -b f bb &&
	(cd sub &&
	 but checkout sub-d) &&
	but cummit -a -m "f" &&

	but checkout -b test-backward e &&
	test_must_fail but merge f)
'


# Check that the conflicting submodule is detected when it is
# in the common ancestor. status should be 'U00...00"
test_expect_success 'but submodule status should display the merge conflict properly with merge base' '
       (cd merge-search &&
       cat >.butmodules <<EOF &&
[submodule "sub"]
       path = sub
       url = $TRASH_DIRECTORY/sub
EOF
       cat >expect <<EOF &&
U$ZERO_OID sub
EOF
       but submodule status > actual &&
       test_cmp expect actual &&
	but reset --hard)
'

# Check that the conflicting submodule is detected when it is
# not in the common ancestor. status should be 'U00...00"
test_expect_success 'but submodule status should display the merge conflict properly without merge-base' '
       (cd merge-search &&
	but checkout -b test-no-merge-base g &&
	test_must_fail but merge b &&
       cat >.butmodules <<EOF &&
[submodule "sub"]
       path = sub
       url = $TRASH_DIRECTORY/sub
EOF
       cat >expect <<EOF &&
U$ZERO_OID sub
EOF
       but submodule status > actual &&
       test_cmp expect actual &&
       but reset --hard)
'


test_expect_success 'merging with a modify/modify conflict between merge bases' '
	but reset --hard HEAD &&
	but checkout -b test2 c &&
	but merge d
'

# canonical criss-cross history in top and submodule
test_expect_success 'setup for recursive merge with submodule' '
	mkdir merge-recursive &&
	(cd merge-recursive &&
	 but init &&
	 mkdir sub &&
	 (cd sub &&
	  but init &&
	  test_cummit a &&
	  but checkout -b sub-b main &&
	  test_cummit b &&
	  but checkout -b sub-c main &&
	  test_cummit c &&
	  but checkout -b sub-bc sub-b &&
	  but merge sub-c &&
	  but checkout -b sub-cb sub-c &&
	  but merge sub-b &&
	  but checkout main) &&
	 but add sub &&
	 but cummit -m a &&
	 but checkout -b top-b main &&
	 (cd sub && but checkout sub-b) &&
	 but add sub &&
	 but cummit -m b &&
	 but checkout -b top-c main &&
	 (cd sub && but checkout sub-c) &&
	 but add sub &&
	 but cummit -m c &&
	 but checkout -b top-bc top-b &&
	 but merge -s ours --no-cummit top-c &&
	 (cd sub && but checkout sub-bc) &&
	 but add sub &&
	 but cummit -m bc &&
	 but checkout -b top-cb top-c &&
	 but merge -s ours --no-cummit top-b &&
	 (cd sub && but checkout sub-cb) &&
	 but add sub &&
	 but cummit -m cb)
'

# merge should leave submodule unmerged in index
test_expect_success 'recursive merge with submodule' '
	(cd merge-recursive &&
	 test_must_fail but merge top-bc &&
	 echo "160000 $(but rev-parse top-cb:sub) 2	sub" > expect2 &&
	 echo "160000 $(but rev-parse top-bc:sub) 3	sub" > expect3 &&
	 but ls-files -u > actual &&
	 grep "$(cat expect2)" actual > /dev/null &&
	 grep "$(cat expect3)" actual > /dev/null)
'

# File/submodule conflict
#   cummit O: <empty>
#   cummit A: path (submodule)
#   cummit B: path
#   Expected: path/ is submodule and file contents for B's path are somewhere

test_expect_success 'setup file/submodule conflict' '
	test_create_repo file-submodule &&
	(
		cd file-submodule &&

		but cummit --allow-empty -m O &&

		but branch A &&
		but branch B &&

		but checkout B &&
		echo content >path &&
		but add path &&
		but cummit -m B &&

		but checkout A &&
		test_create_repo path &&
		test_cummit -C path world &&
		but submodule add ./path &&
		but cummit -m A
	)
'

test_expect_merge_algorithm failure success 'file/submodule conflict' '
	test_when_finished "but -C file-submodule reset --hard" &&
	(
		cd file-submodule &&

		but checkout A^0 &&
		test_must_fail but merge B^0 &&

		but ls-files -s >out &&
		test_line_count = 3 out &&
		but ls-files -u >out &&
		test_line_count = 2 out &&

		# path/ is still a submodule
		test_path_is_dir path/.but &&

		# There is a submodule at "path", so B:path cannot be written
		# there.  We expect it to be written somewhere in the same
		# directory, though, so just grep for its content in all
		# files, and ignore "grep: path: Is a directory" message
		echo Checking if contents from B:path showed up anywhere &&
		grep -q content * 2>/dev/null
	)
'

test_expect_success 'file/submodule conflict; merge --abort works afterward' '
	test_when_finished "but -C file-submodule reset --hard" &&
	(
		cd file-submodule &&

		but checkout A^0 &&
		test_must_fail but merge B^0 >out 2>err &&

		test_path_is_file .but/MERGE_HEAD &&
		but merge --abort
	)
'

# Directory/submodule conflict
#   cummit O: <empty>
#   cummit A: path (submodule), with sole tracked file named 'world'
#   cummit B1: path/file
#   cummit B2: path/world
#
#   Expected from merge of A & B1:
#     Contents under path/ from cummit B1 are renamed elsewhere; we do not
#     want to write files from one of our tracked directories into a submodule
#
#   Expected from merge of A & B2:
#     Similar to last merge, but with a slight twist: we don't want paths
#     under the submodule to be treated as untracked or in the way.

test_expect_success 'setup directory/submodule conflict' '
	test_create_repo directory-submodule &&
	(
		cd directory-submodule &&

		but cummit --allow-empty -m O &&

		but branch A &&
		but branch B1 &&
		but branch B2 &&

		but checkout B1 &&
		mkdir path &&
		echo contents >path/file &&
		but add path/file &&
		but cummit -m B1 &&

		but checkout B2 &&
		mkdir path &&
		echo contents >path/world &&
		but add path/world &&
		but cummit -m B2 &&

		but checkout A &&
		test_create_repo path &&
		test_cummit -C path hello world &&
		but submodule add ./path &&
		but cummit -m A
	)
'

test_expect_failure 'directory/submodule conflict; keep submodule clean' '
	test_when_finished "but -C directory-submodule reset --hard" &&
	(
		cd directory-submodule &&

		but checkout A^0 &&
		test_must_fail but merge B1^0 &&

		but ls-files -s >out &&
		test_line_count = 3 out &&
		but ls-files -u >out &&
		test_line_count = 1 out &&

		# path/ is still a submodule
		test_path_is_dir path/.but &&

		echo Checking if contents from B1:path/file showed up &&
		# Would rather use grep -r, but that is GNU extension...
		but ls-files -co | xargs grep -q contents 2>/dev/null &&

		# However, B1:path/file should NOT have shown up at path/file,
		# because we should not write into the submodule
		test_path_is_missing path/file
	)
'

test_expect_merge_algorithm failure success !FAIL_PREREQS 'directory/submodule conflict; should not treat submodule files as untracked or in the way' '
	test_when_finished "but -C directory-submodule/path reset --hard" &&
	test_when_finished "but -C directory-submodule reset --hard" &&
	(
		cd directory-submodule &&

		but checkout A^0 &&
		test_must_fail but merge B2^0 >out 2>err &&

		# We do not want files within the submodule to prevent the
		# merge from starting; we should not be writing to such paths
		# anyway.
		test_i18ngrep ! "refusing to lose untracked file at" err
	)
'

test_expect_failure 'directory/submodule conflict; merge --abort works afterward' '
	test_when_finished "but -C directory-submodule/path reset --hard" &&
	test_when_finished "but -C directory-submodule reset --hard" &&
	(
		cd directory-submodule &&

		but checkout A^0 &&
		test_must_fail but merge B2^0 &&
		test_path_is_file .but/MERGE_HEAD &&

		# merge --abort should succeed, should clear .but/MERGE_HEAD,
		# and should not leave behind any conflicted files
		but merge --abort &&
		test_path_is_missing .but/MERGE_HEAD &&
		but ls-files -u >conflicts &&
		test_must_be_empty conflicts
	)
'

test_done
