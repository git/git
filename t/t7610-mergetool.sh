#!/bin/sh
#
# Copyright (c) 2008 Charles Bailey
#

test_description='git mergetool

Testing basic merge tool invocation'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# All the mergetool test work by checking out a temporary branch based
# off 'branch1' and then merging in main and checking the results of
# running mergetool

test_expect_success 'setup' '
	test_config rerere.enabled true &&
	echo main >file1 &&
	echo main spaced >"spaced name" &&
	echo main file11 >file11 &&
	echo main file12 >file12 &&
	echo main file13 >file13 &&
	echo main file14 >file14 &&
	mkdir subdir &&
	echo main sub >subdir/file3 &&
	test_create_repo submod &&
	(
		cd submod &&
		: >foo &&
		git add foo &&
		git commit -m "Add foo"
	) &&
	git submodule add file:///dev/null submod &&
	git add file1 "spaced name" file1[1-4] subdir/file3 .gitmodules submod &&
	git commit -m "add initial versions" &&

	git checkout -b branch1 main &&
	git submodule update -N &&
	echo branch1 change >file1 &&
	echo branch1 newfile >file2 &&
	echo branch1 spaced >"spaced name" &&
	echo branch1 both added >both &&
	echo branch1 change file11 >file11 &&
	echo branch1 change file13 >file13 &&
	echo branch1 sub >subdir/file3 &&
	(
		cd submod &&
		echo branch1 submodule >bar &&
		git add bar &&
		git commit -m "Add bar on branch1" &&
		git checkout -b submod-branch1
	) &&
	git add file1 "spaced name" file11 file13 file2 subdir/file3 submod &&
	git add both &&
	git rm file12 &&
	git commit -m "branch1 changes" &&

	git checkout -b delete-base branch1 &&
	mkdir -p a/a &&
	test_write_lines one two 3 4 >a/a/file.txt &&
	git add a/a/file.txt &&
	git commit -m"base file" &&
	git checkout -b move-to-b delete-base &&
	mkdir -p b/b &&
	git mv a/a/file.txt b/b/file.txt &&
	test_write_lines one two 4 >b/b/file.txt &&
	git commit -a -m"move to b" &&
	git checkout -b move-to-c delete-base &&
	mkdir -p c/c &&
	git mv a/a/file.txt c/c/file.txt &&
	test_write_lines one two 3 >c/c/file.txt &&
	git commit -a -m"move to c" &&

	git checkout -b stash1 main &&
	echo stash1 change file11 >file11 &&
	git add file11 &&
	git commit -m "stash1 changes" &&

	git checkout -b stash2 main &&
	echo stash2 change file11 >file11 &&
	git add file11 &&
	git commit -m "stash2 changes" &&

	git checkout main &&
	git submodule update -N &&
	echo main updated >file1 &&
	echo main new >file2 &&
	echo main updated spaced >"spaced name" &&
	echo main both added >both &&
	echo main updated file12 >file12 &&
	echo main updated file14 >file14 &&
	echo main new sub >subdir/file3 &&
	(
		cd submod &&
		echo main submodule >bar &&
		git add bar &&
		git commit -m "Add bar on main" &&
		git checkout -b submod-main
	) &&
	git add file1 "spaced name" file12 file14 file2 subdir/file3 submod &&
	git add both &&
	git rm file11 &&
	git commit -m "main updates" &&

	git clean -fdx &&
	git checkout -b order-file-start main &&
	echo start >a &&
	echo start >b &&
	git add a b &&
	git commit -m start &&
	git checkout -b order-file-side1 order-file-start &&
	echo side1 >a &&
	echo side1 >b &&
	git add a b &&
	git commit -m side1 &&
	git checkout -b order-file-side2 order-file-start &&
	echo side2 >a &&
	echo side2 >b &&
	git add a b &&
	git commit -m side2 &&

	git config merge.tool mytool &&
	git config mergetool.mytool.cmd "cat \"\$REMOTE\" >\"\$MERGED\"" &&
	git config mergetool.mytool.trustExitCode true &&
	git config mergetool.mybase.cmd "cat \"\$BASE\" >\"\$MERGED\"" &&
	git config mergetool.mybase.trustExitCode true
'

test_expect_success 'custom mergetool' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count branch1 &&
	git submodule update -N &&
	test_must_fail git merge main &&
	yes "" | git mergetool both &&
	yes "" | git mergetool file1 file1 &&
	yes "" | git mergetool file2 "spaced name" &&
	yes "" | git mergetool subdir/file3 &&
	yes "d" | git mergetool file11 &&
	yes "d" | git mergetool file12 &&
	yes "l" | git mergetool submod &&
	echo "main updated" >expect &&
	test_cmp expect file1 &&
	echo "main new" >expect &&
	test_cmp expect file2 &&
	echo "main new sub" >expect &&
	test_cmp expect subdir/file3 &&
	echo "branch1 submodule" >expect &&
	test_cmp expect submod/bar &&
	git commit -m "branch1 resolved with mergetool"
'

test_expect_success 'gui mergetool' '
	test_config merge.guitool myguitool &&
	test_config mergetool.myguitool.cmd "(printf \"gui \" && cat \"\$REMOTE\") >\"\$MERGED\"" &&
	test_config mergetool.myguitool.trustExitCode true &&
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count branch1 &&
	git submodule update -N &&
	test_must_fail git merge main &&
	yes "" | git mergetool --gui both &&
	yes "" | git mergetool -g file1 file1 &&
	yes "" | git mergetool --gui file2 "spaced name" &&
	yes "" | git mergetool --gui subdir/file3 &&
	yes "d" | git mergetool --gui file11 &&
	yes "d" | git mergetool --gui file12 &&
	yes "l" | git mergetool --gui submod &&
	echo "gui main updated" >expect &&
	test_cmp expect file1 &&
	echo "gui main new" >expect &&
	test_cmp expect file2 &&
	echo "gui main new sub" >expect &&
	test_cmp expect subdir/file3 &&
	echo "branch1 submodule" >expect &&
	test_cmp expect submod/bar &&
	git commit -m "branch1 resolved with mergetool"
'

test_expect_success 'gui mergetool without merge.guitool set falls back to merge.tool' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count branch1 &&
	git submodule update -N &&
	test_must_fail git merge main &&
	yes "" | git mergetool --gui both &&
	yes "" | git mergetool -g file1 file1 &&
	yes "" | git mergetool --gui file2 "spaced name" &&
	yes "" | git mergetool --gui subdir/file3 &&
	yes "d" | git mergetool --gui file11 &&
	yes "d" | git mergetool --gui file12 &&
	yes "l" | git mergetool --gui submod &&
	echo "main updated" >expect &&
	test_cmp expect file1 &&
	echo "main new" >expect &&
	test_cmp expect file2 &&
	echo "main new sub" >expect &&
	test_cmp expect subdir/file3 &&
	echo "branch1 submodule" >expect &&
	test_cmp expect submod/bar &&
	git commit -m "branch1 resolved with mergetool"
'

test_expect_success 'mergetool crlf' '
	test_when_finished "git reset --hard" &&
	# This test_config line must go after the above reset line so that
	# core.autocrlf is unconfigured before reset runs.  (The
	# test_config command uses test_when_finished internally and
	# test_when_finished is LIFO.)
	test_config core.autocrlf true &&
	git checkout -b test$test_count branch1 &&
	test_must_fail git merge main &&
	yes "" | git mergetool file1 &&
	yes "" | git mergetool file2 &&
	yes "" | git mergetool "spaced name" &&
	yes "" | git mergetool both &&
	yes "" | git mergetool subdir/file3 &&
	yes "d" | git mergetool file11 &&
	yes "d" | git mergetool file12 &&
	yes "r" | git mergetool submod &&
	test "$(printf x | cat file1 -)" = "$(printf "main updated\r\nx")" &&
	test "$(printf x | cat file2 -)" = "$(printf "main new\r\nx")" &&
	test "$(printf x | cat subdir/file3 -)" = "$(printf "main new sub\r\nx")" &&
	git submodule update -N &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&
	git commit -m "branch1 resolved with mergetool - autocrlf"
'

test_expect_success 'mergetool in subdir' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count branch1 &&
	git submodule update -N &&
	(
		cd subdir &&
		test_must_fail git merge main &&
		yes "" | git mergetool file3 &&
		echo "main new sub" >expect &&
		test_cmp expect file3
	)
'

test_expect_success 'mergetool on file in parent dir' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count branch1 &&
	git submodule update -N &&
	(
		cd subdir &&
		test_must_fail git merge main &&
		yes "" | git mergetool file3 &&
		yes "" | git mergetool ../file1 &&
		yes "" | git mergetool ../file2 ../spaced\ name &&
		yes "" | git mergetool ../both &&
		yes "d" | git mergetool ../file11 &&
		yes "d" | git mergetool ../file12 &&
		yes "l" | git mergetool ../submod &&
		echo "main updated" >expect &&
		test_cmp expect ../file1 &&
		echo "main new" >expect &&
		test_cmp expect ../file2 &&
		echo "branch1 submodule" >expect &&
		test_cmp expect ../submod/bar &&
		git commit -m "branch1 resolved with mergetool - subdir"
	)
'

test_expect_success 'mergetool skips autoresolved' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count branch1 &&
	git submodule update -N &&
	test_must_fail git merge main &&
	test -n "$(git ls-files -u)" &&
	yes "d" | git mergetool file11 &&
	yes "d" | git mergetool file12 &&
	yes "l" | git mergetool submod &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging"
'

test_expect_success 'mergetool merges all from subdir (rerere disabled)' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count branch1 &&
	test_config rerere.enabled false &&
	(
		cd subdir &&
		test_must_fail git merge main &&
		yes "r" | git mergetool ../submod &&
		yes "d" "d" | git mergetool --no-prompt &&
		echo "main updated" >expect &&
		test_cmp expect ../file1 &&
		echo "main new" >expect &&
		test_cmp expect ../file2 &&
		echo "main new sub" >expect &&
		test_cmp expect file3 &&
		( cd .. && git submodule update -N ) &&
		echo "main submodule" >expect &&
		test_cmp expect ../submod/bar &&
		git commit -m "branch2 resolved by mergetool from subdir"
	)
'

test_expect_success 'mergetool merges all from subdir (rerere enabled)' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count branch1 &&
	test_config rerere.enabled true &&
	rm -rf .git/rr-cache &&
	(
		cd subdir &&
		test_must_fail git merge main &&
		yes "r" | git mergetool ../submod &&
		yes "d" "d" | git mergetool --no-prompt &&
		echo "main updated" >expect &&
		test_cmp expect ../file1 &&
		echo "main new" >expect &&
		test_cmp expect ../file2 &&
		echo "main new sub" >expect &&
		test_cmp expect file3 &&
		( cd .. && git submodule update -N ) &&
		echo "main submodule" >expect &&
		test_cmp expect ../submod/bar &&
		git commit -m "branch2 resolved by mergetool from subdir"
	)
'

test_expect_success 'mergetool skips resolved paths when rerere is active' '
	test_when_finished "git reset --hard" &&
	test_config rerere.enabled true &&
	rm -rf .git/rr-cache &&
	git checkout -b test$test_count branch1 &&
	git submodule update -N &&
	test_must_fail git merge main &&
	yes "l" | git mergetool --no-prompt submod &&
	yes "d" "d" | git mergetool --no-prompt &&
	git submodule update -N &&
	output="$(yes "n" | git mergetool --no-prompt)" &&
	test "$output" = "No files need merging"
'

test_expect_success 'conflicted stash sets up rerere'  '
	test_when_finished "git reset --hard" &&
	test_config rerere.enabled true &&
	git checkout stash1 &&
	echo "Conflicting stash content" >file11 &&
	git stash &&

	git checkout --detach stash2 &&
	test_must_fail git stash apply &&

	test -n "$(git ls-files -u)" &&
	conflicts="$(git rerere remaining)" &&
	test "$conflicts" = "file11" &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" != "No files need merging" &&

	git commit -am "save the stash resolution" &&

	git reset --hard stash2 &&
	test_must_fail git stash apply &&

	test -n "$(git ls-files -u)" &&
	conflicts="$(git rerere remaining)" &&
	test -z "$conflicts" &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging"
'

test_expect_success 'mergetool takes partial path' '
	test_when_finished "git reset --hard" &&
	test_config rerere.enabled false &&
	git checkout -b test$test_count branch1 &&
	git submodule update -N &&
	test_must_fail git merge main &&

	yes "" | git mergetool subdir &&

	echo "main new sub" >expect &&
	test_cmp expect subdir/file3
'

test_expect_success 'mergetool delete/delete conflict' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count move-to-c &&
	test_must_fail git merge move-to-b &&
	echo d | git mergetool a/a/file.txt &&
	! test -f a/a/file.txt &&
	git reset --hard &&
	test_must_fail git merge move-to-b &&
	echo m | git mergetool a/a/file.txt &&
	test -f b/b/file.txt &&
	git reset --hard &&
	test_must_fail git merge move-to-b &&
	! echo a | git mergetool a/a/file.txt &&
	! test -f a/a/file.txt
'

test_expect_success 'mergetool produces no errors when keepBackup is used' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count move-to-c &&
	test_config mergetool.keepBackup true &&
	test_must_fail git merge move-to-b &&
	echo d | git mergetool a/a/file.txt 2>actual &&
	test_must_be_empty actual &&
	! test -d a
'

test_expect_success 'mergetool honors tempfile config for deleted files' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count move-to-c &&
	test_config mergetool.keepTemporaries false &&
	test_must_fail git merge move-to-b &&
	echo d | git mergetool a/a/file.txt &&
	! test -d a
'

test_expect_success 'mergetool keeps tempfiles when aborting delete/delete' '
	test_when_finished "git reset --hard" &&
	test_when_finished "git clean -fdx" &&
	git checkout -b test$test_count move-to-c &&
	test_config mergetool.keepTemporaries true &&
	test_must_fail git merge move-to-b &&
	! test_write_lines a n | git mergetool a/a/file.txt &&
	test -d a/a &&
	cat >expect <<-\EOF &&
	file_BASE_.txt
	file_LOCAL_.txt
	file_REMOTE_.txt
	EOF
	ls -1 a/a | sed -e "s/[0-9]*//g" >actual &&
	test_cmp expect actual
'

test_expect_success 'deleted vs modified submodule' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count branch1 &&
	git submodule update -N &&
	mv submod submod-movedaside &&
	git rm --cached submod &&
	git commit -m "Submodule deleted from branch" &&
	git checkout -b test$test_count.a test$test_count &&
	test_must_fail git merge main &&
	test -n "$(git ls-files -u)" &&
	yes "" | git mergetool file1 file2 spaced\ name subdir/file3 &&
	yes "" | git mergetool both &&
	yes "d" | git mergetool file11 file12 &&
	yes "r" | git mergetool submod &&
	rmdir submod && mv submod-movedaside submod &&
	echo "branch1 submodule" >expect &&
	test_cmp expect submod/bar &&
	git submodule update -N &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git commit -m "Merge resolved by keeping module" &&

	mv submod submod-movedaside &&
	git checkout -b test$test_count.b test$test_count &&
	git submodule update -N &&
	test_must_fail git merge main &&
	test -n "$(git ls-files -u)" &&
	yes "" | git mergetool file1 file2 spaced\ name subdir/file3 &&
	yes "" | git mergetool both &&
	yes "d" | git mergetool file11 file12 &&
	yes "l" | git mergetool submod &&
	test ! -e submod &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git commit -m "Merge resolved by deleting module" &&

	mv submod-movedaside submod &&
	git checkout -b test$test_count.c main &&
	git submodule update -N &&
	test_must_fail git merge test$test_count &&
	test -n "$(git ls-files -u)" &&
	yes "" | git mergetool file1 file2 spaced\ name subdir/file3 &&
	yes "" | git mergetool both &&
	yes "d" | git mergetool file11 file12 &&
	yes "r" | git mergetool submod &&
	test ! -e submod &&
	test -d submod.orig &&
	git submodule update -N &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git commit -m "Merge resolved by deleting module" &&
	mv submod.orig submod &&

	git checkout -b test$test_count.d main &&
	git submodule update -N &&
	test_must_fail git merge test$test_count &&
	test -n "$(git ls-files -u)" &&
	yes "" | git mergetool file1 file2 spaced\ name subdir/file3 &&
	yes "" | git mergetool both &&
	yes "d" | git mergetool file11 file12 &&
	yes "l" | git mergetool submod &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&
	git submodule update -N &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git commit -m "Merge resolved by keeping module"
'

test_expect_success 'file vs modified submodule' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count branch1 &&
	git submodule update -N &&
	mv submod submod-movedaside &&
	git rm --cached submod &&
	echo not a submodule >submod &&
	git add submod &&
	git commit -m "Submodule path becomes file" &&
	git checkout -b test$test_count.a branch1 &&
	test_must_fail git merge main &&
	test -n "$(git ls-files -u)" &&
	yes "" | git mergetool file1 file2 spaced\ name subdir/file3 &&
	yes "" | git mergetool both &&
	yes "d" | git mergetool file11 file12 &&
	yes "r" | git mergetool submod &&
	rmdir submod && mv submod-movedaside submod &&
	echo "branch1 submodule" >expect &&
	test_cmp expect submod/bar &&
	git submodule update -N &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git commit -m "Merge resolved by keeping module" &&

	mv submod submod-movedaside &&
	git checkout -b test$test_count.b test$test_count &&
	test_must_fail git merge main &&
	test -n "$(git ls-files -u)" &&
	yes "" | git mergetool file1 file2 spaced\ name subdir/file3 &&
	yes "" | git mergetool both &&
	yes "d" | git mergetool file11 file12 &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		yes "c" | git mergetool submod~HEAD &&
		git rm submod &&
		git mv submod~HEAD submod
	else
		yes "l" | git mergetool submod
	fi &&
	git submodule update -N &&
	echo "not a submodule" >expect &&
	test_cmp expect submod &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git commit -m "Merge resolved by keeping file" &&

	git checkout -b test$test_count.c main &&
	rmdir submod && mv submod-movedaside submod &&
	test ! -e submod.orig &&
	git submodule update -N &&
	test_must_fail git merge test$test_count &&
	test -n "$(git ls-files -u)" &&
	yes "" | git mergetool file1 file2 spaced\ name subdir/file3 &&
	yes "" | git mergetool both &&
	yes "d" | git mergetool file11 file12 &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		mv submod submod.orig &&
		git rm --cached submod &&
		yes "c" | git mergetool submod~test19 &&
		git mv submod~test19 submod
	else
		yes "r" | git mergetool submod
	fi &&
	test -d submod.orig &&
	git submodule update -N &&
	echo "not a submodule" >expect &&
	test_cmp expect submod &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git commit -m "Merge resolved by keeping file" &&

	git checkout -b test$test_count.d main &&
	rmdir submod && mv submod.orig submod &&
	git submodule update -N &&
	test_must_fail git merge test$test_count &&
	test -n "$(git ls-files -u)" &&
	yes "" | git mergetool file1 file2 spaced\ name subdir/file3 &&
	yes "" | git mergetool both &&
	yes "d" | git mergetool file11 file12 &&
	yes "l" | git mergetool submod &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		yes "d" | git mergetool submod~test19
	fi &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&
	git submodule update -N &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git commit -m "Merge resolved by keeping module"
'

test_expect_success 'submodule in subdirectory' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count branch1 &&
	git submodule update -N &&
	(
		cd subdir &&
		test_create_repo subdir_module &&
		(
		cd subdir_module &&
		: >file15 &&
		git add file15 &&
		git commit -m "add initial versions"
		)
	) &&
	test_when_finished "rm -rf subdir/subdir_module" &&
	git submodule add file:///dev/null subdir/subdir_module &&
	git add subdir/subdir_module &&
	git commit -m "add submodule in subdirectory" &&

	git checkout -b test$test_count.a test$test_count &&
	git submodule update -N &&
	(
	cd subdir/subdir_module &&
		git checkout -b super10.a &&
		echo test$test_count.a >file15 &&
		git add file15 &&
		git commit -m "on branch 10.a"
	) &&
	git add subdir/subdir_module &&
	git commit -m "change submodule in subdirectory on test$test_count.a" &&

	git checkout -b test$test_count.b test$test_count &&
	git submodule update -N &&
	(
		cd subdir/subdir_module &&
		git checkout -b super10.b &&
		echo test$test_count.b >file15 &&
		git add file15 &&
		git commit -m "on branch 10.b"
	) &&
	git add subdir/subdir_module &&
	git commit -m "change submodule in subdirectory on test$test_count.b" &&

	test_must_fail git merge test$test_count.a &&
	(
		cd subdir &&
		yes "l" | git mergetool subdir_module
	) &&
	echo "test$test_count.b" >expect &&
	test_cmp expect subdir/subdir_module/file15 &&
	git submodule update -N &&
	echo "test$test_count.b" >expect &&
	test_cmp expect subdir/subdir_module/file15 &&
	git reset --hard &&
	git submodule update -N &&

	test_must_fail git merge test$test_count.a &&
	yes "r" | git mergetool subdir/subdir_module &&
	echo "test$test_count.b" >expect &&
	test_cmp expect subdir/subdir_module/file15 &&
	git submodule update -N &&
	echo "test$test_count.a" >expect &&
	test_cmp expect subdir/subdir_module/file15 &&
	git commit -m "branch1 resolved with mergetool"
'

test_expect_success 'directory vs modified submodule' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count branch1 &&
	mv submod submod-movedaside &&
	git rm --cached submod &&
	mkdir submod &&
	echo not a submodule >submod/file16 &&
	git add submod/file16 &&
	git commit -m "Submodule path becomes directory" &&

	test_must_fail git merge main &&
	test -n "$(git ls-files -u)" &&
	yes "l" | git mergetool submod &&
	echo "not a submodule" >expect &&
	test_cmp expect submod/file16 &&
	rm -rf submod.orig &&

	git reset --hard &&
	test_must_fail git merge main &&
	test -n "$(git ls-files -u)" &&
	test ! -e submod.orig &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		yes "r" | git mergetool submod~main &&
		git mv submod submod.orig &&
		git mv submod~main submod
	else
		yes "r" | git mergetool submod
	fi &&
	test -d submod.orig &&
	echo "not a submodule" >expect &&
	test_cmp expect submod.orig/file16 &&
	rm -r submod.orig &&
	mv submod-movedaside/.git submod &&
	( cd submod && git clean -f && git reset --hard ) &&
	git submodule update -N &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&
	git reset --hard &&
	rm -rf submod-movedaside &&

	git checkout -b test$test_count.c main &&
	git submodule update -N &&
	test_must_fail git merge test$test_count &&
	test -n "$(git ls-files -u)" &&
	yes "l" | git mergetool submod &&
	git submodule update -N &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&

	git reset --hard &&
	git submodule update -N &&
	test_must_fail git merge test$test_count &&
	test -n "$(git ls-files -u)" &&
	test ! -e submod.orig &&
	yes "r" | git mergetool submod &&
	echo "not a submodule" >expect &&
	test_cmp expect submod/file16 &&

	git reset --hard main &&
	( cd submod && git clean -f && git reset --hard ) &&
	git submodule update -N
'

test_expect_success 'file with no base' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count branch1 &&
	test_must_fail git merge main &&
	git mergetool --no-prompt --tool mybase -- both &&
	test_must_be_empty both
'

test_expect_success 'custom commands override built-ins' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count branch1 &&
	test_config mergetool.defaults.cmd "cat \"\$REMOTE\" >\"\$MERGED\"" &&
	test_config mergetool.defaults.trustExitCode true &&
	test_must_fail git merge main &&
	git mergetool --no-prompt --tool defaults -- both &&
	echo main both added >expected &&
	test_cmp expected both
'

test_expect_success 'filenames seen by tools start with ./' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count branch1 &&
	test_config mergetool.writeToTemp false &&
	test_config mergetool.myecho.cmd "echo \"\$LOCAL\"" &&
	test_config mergetool.myecho.trustExitCode true &&
	test_must_fail git merge main &&
	git mergetool --no-prompt --tool myecho -- both >actual &&
	grep ^\./both_LOCAL_ actual
'

test_lazy_prereq MKTEMP '
	tempdir=$(mktemp -d -t foo.XXXXXX) &&
	test -d "$tempdir" &&
	rmdir "$tempdir"
'

test_expect_success MKTEMP 'temporary filenames are used with mergetool.writeToTemp' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count branch1 &&
	test_config mergetool.writeToTemp true &&
	test_config mergetool.myecho.cmd "echo \"\$LOCAL\"" &&
	test_config mergetool.myecho.trustExitCode true &&
	test_must_fail git merge main &&
	git mergetool --no-prompt --tool myecho -- both >actual &&
	! grep ^\./both_LOCAL_ actual &&
	grep /both_LOCAL_ actual
'

test_expect_success 'diff.orderFile configuration is honored' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count order-file-side2 &&
	test_config diff.orderFile order-file &&
	test_config mergetool.myecho.cmd "echo \"\$LOCAL\"" &&
	test_config mergetool.myecho.trustExitCode true &&
	echo b >order-file &&
	echo a >>order-file &&
	test_must_fail git merge order-file-side1 &&
	cat >expect <<-\EOF &&
		Merging:
		b
		a
	EOF

	# make sure "order-file" that is ambiguous between
	# rev and path is understood correctly.
	git branch order-file HEAD &&

	git mergetool --no-prompt --tool myecho >output &&
	git grep --no-index -h -A2 Merging: output >actual &&
	test_cmp expect actual
'
test_expect_success 'mergetool -Oorder-file is honored' '
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count order-file-side2 &&
	test_config diff.orderFile order-file &&
	test_config mergetool.myecho.cmd "echo \"\$LOCAL\"" &&
	test_config mergetool.myecho.trustExitCode true &&
	echo b >order-file &&
	echo a >>order-file &&
	test_must_fail git merge order-file-side1 &&
	cat >expect <<-\EOF &&
		Merging:
		a
		b
	EOF
	git mergetool -O/dev/null --no-prompt --tool myecho >output &&
	git grep --no-index -h -A2 Merging: output >actual &&
	test_cmp expect actual &&
	git reset --hard &&

	git config --unset diff.orderFile &&
	test_must_fail git merge order-file-side1 &&
	cat >expect <<-\EOF &&
		Merging:
		b
		a
	EOF
	git mergetool -Oorder-file --no-prompt --tool myecho >output &&
	git grep --no-index -h -A2 Merging: output >actual &&
	test_cmp expect actual
'

test_expect_success 'mergetool --tool-help shows recognized tools' '
	# Check a few known tools are correctly shown
	git mergetool --tool-help >mergetools &&
	grep vimdiff mergetools &&
	grep vimdiff3 mergetools &&
	grep gvimdiff2 mergetools &&
	grep araxis mergetools &&
	grep xxdiff mergetools &&
	grep meld mergetools
'

test_expect_success 'mergetool hideResolved' '
	test_config mergetool.hideResolved true &&
	test_when_finished "git reset --hard" &&
	git checkout -b test${test_count}_b main &&
	test_write_lines >file1 base "" a &&
	git commit -a -m "base" &&
	test_write_lines >file1 base "" c &&
	git commit -a -m "remote update" &&
	git checkout -b test${test_count}_a HEAD~ &&
	test_write_lines >file1 local "" b &&
	git commit -a -m "local update" &&
	test_must_fail git merge test${test_count}_b &&
	yes "" | git mergetool file1 &&
	test_write_lines >expect local "" c &&
	test_cmp expect file1 &&
	git commit -m "test resolved with mergetool"
'

test_expect_success 'mergetool with guiDefault' '
	test_config merge.guitool myguitool &&
	test_config mergetool.myguitool.cmd "(printf \"gui \" && cat \"\$REMOTE\") >\"\$MERGED\"" &&
	test_config mergetool.myguitool.trustExitCode true &&
	test_when_finished "git reset --hard" &&
	git checkout -b test$test_count branch1 &&
	git submodule update -N &&
	test_must_fail git merge main &&

	test_config mergetool.guiDefault auto &&
	DISPLAY=SOMETHING && export DISPLAY &&
	yes "" | git mergetool both &&
	yes "" | git mergetool file1 file1 &&

	DISPLAY= && export DISPLAY &&
	yes "" | git mergetool file2 "spaced name" &&

	test_config mergetool.guiDefault true &&
	yes "" | git mergetool subdir/file3 &&

	yes "d" | git mergetool file11 &&
	yes "d" | git mergetool file12 &&
	yes "l" | git mergetool submod &&

	echo "gui main updated" >expect &&
	test_cmp expect file1 &&

	echo "main new" >expect &&
	test_cmp expect file2 &&

	echo "gui main new sub" >expect &&
	test_cmp expect subdir/file3 &&

	echo "branch1 submodule" >expect &&
	test_cmp expect submod/bar &&
	git commit -m "branch1 resolved with mergetool"
'

test_done
