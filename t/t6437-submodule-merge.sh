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
	 git init &&
	 echo original > file &&
	 git add file &&
	 test_tick &&
	 git commit -m sub-root) &&
	git add sub &&
	test_tick &&
	git commit -m root &&

	git checkout -b a main &&
	(cd sub &&
	 echo A > file &&
	 git add file &&
	 test_tick &&
	 git commit -m sub-a) &&
	git add sub &&
	test_tick &&
	git commit -m a &&

	git checkout -b b main &&
	(cd sub &&
	 echo B > file &&
	 git add file &&
	 test_tick &&
	 git commit -m sub-b) &&
	git add sub &&
	test_tick &&
	git commit -m b &&

	git checkout -b c a &&
	git merge -s ours b &&

	git checkout -b d b &&
	git merge -s ours a
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
	git init &&
	mkdir sub &&
	(cd sub &&
	 git init &&
	 echo "file-a" > file-a &&
	 git add file-a &&
	 git commit -m "sub-a" &&
	 git branch sub-a) &&
	git commit --allow-empty -m init &&
	git branch init &&
	git add sub &&
	git commit -m "a" &&
	git branch a &&

	git checkout -b b &&
	(cd sub &&
	 git checkout -b sub-b &&
	 echo "file-b" > file-b &&
	 git add file-b &&
	 git commit -m "sub-b") &&
	git commit -a -m "b" &&

	git checkout -b c a &&
	(cd sub &&
	 git checkout -b sub-c sub-a &&
	 echo "file-c" > file-c &&
	 git add file-c &&
	 git commit -m "sub-c") &&
	git commit -a -m "c")
'

test_expect_success 'merging should conflict for non fast-forward' '
	test_when_finished "git -C merge-search reset --hard" &&
	(cd merge-search &&
	 git checkout -b test-nonforward-a b &&
	  if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	  then
		test_must_fail git merge c >actual &&
		sub_expect="go to submodule (sub), and either merge commit $(git -C sub rev-parse --short sub-c)" &&
		grep "$sub_expect" actual
	  else
		test_must_fail git merge c 2> actual
	  fi)
'

test_expect_success 'finish setup for merge-search' '
	(cd merge-search &&
	git checkout -b d a &&
	(cd sub &&
	 git checkout -b sub-d sub-b &&
	 git merge sub-c) &&
	git commit -a -m "d" &&
	git branch test b &&

	git checkout -b g init &&
	(cd sub &&
	 git checkout -b sub-g sub-c) &&
	git add sub &&
	git commit -a -m "g")
'

test_expect_success 'merge with one side as a fast-forward of the other' '
	(cd merge-search &&
	 git checkout -b test-forward b &&
	 git merge d &&
	 git ls-tree test-forward sub | cut -f1 | cut -f3 -d" " > actual &&
	 (cd sub &&
	  git rev-parse sub-d > ../expect) &&
	 test_cmp expect actual)
'

test_expect_success 'merging should conflict for non fast-forward (resolution exists)' '
	(cd merge-search &&
	 git checkout -b test-nonforward-b b &&
	 (cd sub &&
	  git rev-parse --short sub-d > ../expect) &&
	  if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	  then
		test_must_fail git merge c >actual &&
		sub_expect="go to submodule (sub), and either merge commit $(git -C sub rev-parse --short sub-c)" &&
		grep "$sub_expect" actual
	  else
		test_must_fail git merge c 2> actual
	  fi &&
	 grep $(cat expect) actual > /dev/null &&
	 git reset --hard)
'

test_expect_success 'merging should fail for ambiguous common parent' '
	(cd merge-search &&
	git checkout -b test-ambiguous b &&
	(cd sub &&
	 git checkout -b ambiguous sub-b &&
	 git merge sub-c &&
	 if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	 then
		git rev-parse --short sub-d >../expect1 &&
		git rev-parse --short ambiguous >../expect2
	 else
		git rev-parse sub-d > ../expect1 &&
		git rev-parse ambiguous > ../expect2
	 fi
	 ) &&
	 if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	 then
		test_must_fail git merge c >actual &&
		sub_expect="go to submodule (sub), and either merge commit $(git -C sub rev-parse --short sub-c)" &&
		grep "$sub_expect" actual
	 else
		test_must_fail git merge c 2> actual
	 fi &&
	grep $(cat expect1) actual > /dev/null &&
	grep $(cat expect2) actual > /dev/null &&
	git reset --hard)
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
# commits (sub-a) does not descend from the submodule merge-base (sub-b).
#
test_expect_success 'merging should fail for changes that are backwards' '
	(cd merge-search &&
	git checkout -b bb a &&
	(cd sub &&
	 git checkout sub-b) &&
	git commit -a -m "bb" &&

	git checkout -b e bb &&
	(cd sub &&
	 git checkout sub-a) &&
	git commit -a -m "e" &&

	git checkout -b f bb &&
	(cd sub &&
	 git checkout sub-d) &&
	git commit -a -m "f" &&

	git checkout -b test-backward e &&
	test_must_fail git merge f >actual &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
    then
		sub_expect="go to submodule (sub), and either merge commit $(git -C sub rev-parse --short sub-d)" &&
		grep "$sub_expect" actual
	fi)
'


# Check that the conflicting submodule is detected when it is
# in the common ancestor. status should be 'U00...00"
test_expect_success 'git submodule status should display the merge conflict properly with merge base' '
       (cd merge-search &&
       cat >.gitmodules <<EOF &&
[submodule "sub"]
       path = sub
       url = $TRASH_DIRECTORY/sub
EOF
       cat >expect <<EOF &&
U$ZERO_OID sub
EOF
       git submodule status > actual &&
       test_cmp expect actual &&
	git reset --hard)
'

# Check that the conflicting submodule is detected when it is
# not in the common ancestor. status should be 'U00...00"
test_expect_success 'git submodule status should display the merge conflict properly without merge-base' '
       (cd merge-search &&
	git checkout -b test-no-merge-base g &&
	test_must_fail git merge b &&
       cat >.gitmodules <<EOF &&
[submodule "sub"]
       path = sub
       url = $TRASH_DIRECTORY/sub
EOF
       cat >expect <<EOF &&
U$ZERO_OID sub
EOF
       git submodule status > actual &&
       test_cmp expect actual &&
       git reset --hard)
'


test_expect_success 'merging with a modify/modify conflict between merge bases' '
	git reset --hard HEAD &&
	git checkout -b test2 c &&
	git merge d
'

# canonical criss-cross history in top and submodule
test_expect_success 'setup for recursive merge with submodule' '
	mkdir merge-recursive &&
	(cd merge-recursive &&
	 git init &&
	 mkdir sub &&
	 (cd sub &&
	  git init &&
	  test_commit a &&
	  git checkout -b sub-b main &&
	  test_commit b &&
	  git checkout -b sub-c main &&
	  test_commit c &&
	  git checkout -b sub-bc sub-b &&
	  git merge sub-c &&
	  git checkout -b sub-cb sub-c &&
	  git merge sub-b &&
	  git checkout main) &&
	 git add sub &&
	 git commit -m a &&
	 git checkout -b top-b main &&
	 (cd sub && git checkout sub-b) &&
	 git add sub &&
	 git commit -m b &&
	 git checkout -b top-c main &&
	 (cd sub && git checkout sub-c) &&
	 git add sub &&
	 git commit -m c &&
	 git checkout -b top-bc top-b &&
	 git merge -s ours --no-commit top-c &&
	 (cd sub && git checkout sub-bc) &&
	 git add sub &&
	 git commit -m bc &&
	 git checkout -b top-cb top-c &&
	 git merge -s ours --no-commit top-b &&
	 (cd sub && git checkout sub-cb) &&
	 git add sub &&
	 git commit -m cb)
'

# merge should leave submodule unmerged in index
test_expect_success 'recursive merge with submodule' '
	(cd merge-recursive &&
	 test_must_fail git merge top-bc &&
	 echo "160000 $(git rev-parse top-cb:sub) 2	sub" > expect2 &&
	 echo "160000 $(git rev-parse top-bc:sub) 3	sub" > expect3 &&
	 git ls-files -u > actual &&
	 grep "$(cat expect2)" actual > /dev/null &&
	 grep "$(cat expect3)" actual > /dev/null)
'

# File/submodule conflict
#   Commit O: <empty>
#   Commit A: path (submodule)
#   Commit B: path
#   Expected: path/ is submodule and file contents for B's path are somewhere

test_expect_success 'setup file/submodule conflict' '
	test_create_repo file-submodule &&
	(
		cd file-submodule &&

		git commit --allow-empty -m O &&

		git branch A &&
		git branch B &&

		git checkout B &&
		echo content >path &&
		git add path &&
		git commit -m B &&

		git checkout A &&
		test_create_repo path &&
		test_commit -C path world &&
		git submodule add ./path &&
		git commit -m A
	)
'

test_expect_merge_algorithm failure success 'file/submodule conflict' '
	test_when_finished "git -C file-submodule reset --hard" &&
	(
		cd file-submodule &&

		git checkout A^0 &&
		test_must_fail git merge B^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&

		# path/ is still a submodule
		test_path_is_dir path/.git &&

		# There is a submodule at "path", so B:path cannot be written
		# there.  We expect it to be written somewhere in the same
		# directory, though, so just grep for its content in all
		# files, and ignore "grep: path: Is a directory" message
		echo Checking if contents from B:path showed up anywhere &&
		grep -q content * 2>/dev/null
	)
'

test_expect_success 'file/submodule conflict; merge --abort works afterward' '
	test_when_finished "git -C file-submodule reset --hard" &&
	(
		cd file-submodule &&

		git checkout A^0 &&
		test_must_fail git merge B^0 >out 2>err &&

		test_path_is_file .git/MERGE_HEAD &&
		git merge --abort
	)
'

# Directory/submodule conflict
#   Commit O: <empty>
#   Commit A: path (submodule), with sole tracked file named 'world'
#   Commit B1: path/file
#   Commit B2: path/world
#
#   Expected from merge of A & B1:
#     Contents under path/ from commit B1 are renamed elsewhere; we do not
#     want to write files from one of our tracked directories into a submodule
#
#   Expected from merge of A & B2:
#     Similar to last merge, but with a slight twist: we don't want paths
#     under the submodule to be treated as untracked or in the way.

test_expect_success 'setup directory/submodule conflict' '
	test_create_repo directory-submodule &&
	(
		cd directory-submodule &&

		git commit --allow-empty -m O &&

		git branch A &&
		git branch B1 &&
		git branch B2 &&

		git checkout B1 &&
		mkdir path &&
		echo contents >path/file &&
		git add path/file &&
		git commit -m B1 &&

		git checkout B2 &&
		mkdir path &&
		echo contents >path/world &&
		git add path/world &&
		git commit -m B2 &&

		git checkout A &&
		test_create_repo path &&
		test_commit -C path hello world &&
		git submodule add ./path &&
		git commit -m A
	)
'

test_expect_failure 'directory/submodule conflict; keep submodule clean' '
	test_when_finished "git -C directory-submodule reset --hard" &&
	(
		cd directory-submodule &&

		git checkout A^0 &&
		test_must_fail git merge B1^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 1 out &&

		# path/ is still a submodule
		test_path_is_dir path/.git &&

		echo Checking if contents from B1:path/file showed up &&
		# Would rather use grep -r, but that is GNU extension...
		git ls-files -co | xargs grep -q contents 2>/dev/null &&

		# However, B1:path/file should NOT have shown up at path/file,
		# because we should not write into the submodule
		test_path_is_missing path/file
	)
'

test_expect_merge_algorithm failure success !FAIL_PREREQS 'directory/submodule conflict; should not treat submodule files as untracked or in the way' '
	test_when_finished "git -C directory-submodule/path reset --hard" &&
	test_when_finished "git -C directory-submodule reset --hard" &&
	(
		cd directory-submodule &&

		git checkout A^0 &&
		test_must_fail git merge B2^0 >out 2>err &&

		# We do not want files within the submodule to prevent the
		# merge from starting; we should not be writing to such paths
		# anyway.
		test_i18ngrep ! "refusing to lose untracked file at" err
	)
'

test_expect_failure 'directory/submodule conflict; merge --abort works afterward' '
	test_when_finished "git -C directory-submodule/path reset --hard" &&
	test_when_finished "git -C directory-submodule reset --hard" &&
	(
		cd directory-submodule &&

		git checkout A^0 &&
		test_must_fail git merge B2^0 &&
		test_path_is_file .git/MERGE_HEAD &&

		# merge --abort should succeed, should clear .git/MERGE_HEAD,
		# and should not leave behind any conflicted files
		git merge --abort &&
		test_path_is_missing .git/MERGE_HEAD &&
		git ls-files -u >conflicts &&
		test_must_be_empty conflicts
	)
'

# Setup:
#   - Submodule has 2 commits: a and b
#   - Superproject branch 'a' adds and commits submodule pointing to 'commit a'
#   - Superproject branch 'b' adds and commits submodule pointing to 'commit b'
# If these two branches are now merged, there is no merge base
test_expect_success 'setup for null merge base' '
	mkdir no-merge-base &&
	(cd no-merge-base &&
	git init &&
	mkdir sub &&
	(cd sub &&
	 git init &&
	 echo "file-a" > file-a &&
	 git add file-a &&
	 git commit -m "commit a") &&
	git commit --allow-empty -m init &&
	git branch init &&
	git checkout -b a init &&
	git add sub &&
	git commit -m "a" &&
	git switch main &&
	(cd sub &&
	 echo "file-b" > file-b &&
	 git add file-b &&
	 git commit -m "commit b"))
'

test_expect_success 'merging should fail with no merge base' '
	(cd no-merge-base &&
	git checkout -b b init &&
	git add sub &&
	git commit -m "b" &&
	test_must_fail git merge a >actual &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
    then
		sub_expect="go to submodule (sub), and either merge commit $(git -C sub rev-parse --short HEAD^1)" &&
		grep "$sub_expect" actual
	fi)
'

test_done
