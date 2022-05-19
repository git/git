#!/bin/sh
#
# Copyright (c) 2008 Charles Bailey
#

test_description='but mergetool

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
		but add foo &&
		but cummit -m "Add foo"
	) &&
	but submodule add but://example.com/submod submod &&
	but add file1 "spaced name" file1[1-4] subdir/file3 .butmodules submod &&
	but cummit -m "add initial versions" &&

	but checkout -b branch1 main &&
	but submodule update -N &&
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
		but add bar &&
		but cummit -m "Add bar on branch1" &&
		but checkout -b submod-branch1
	) &&
	but add file1 "spaced name" file11 file13 file2 subdir/file3 submod &&
	but add both &&
	but rm file12 &&
	but cummit -m "branch1 changes" &&

	but checkout -b delete-base branch1 &&
	mkdir -p a/a &&
	test_write_lines one two 3 4 >a/a/file.txt &&
	but add a/a/file.txt &&
	but cummit -m"base file" &&
	but checkout -b move-to-b delete-base &&
	mkdir -p b/b &&
	but mv a/a/file.txt b/b/file.txt &&
	test_write_lines one two 4 >b/b/file.txt &&
	but cummit -a -m"move to b" &&
	but checkout -b move-to-c delete-base &&
	mkdir -p c/c &&
	but mv a/a/file.txt c/c/file.txt &&
	test_write_lines one two 3 >c/c/file.txt &&
	but cummit -a -m"move to c" &&

	but checkout -b stash1 main &&
	echo stash1 change file11 >file11 &&
	but add file11 &&
	but cummit -m "stash1 changes" &&

	but checkout -b stash2 main &&
	echo stash2 change file11 >file11 &&
	but add file11 &&
	but cummit -m "stash2 changes" &&

	but checkout main &&
	but submodule update -N &&
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
		but add bar &&
		but cummit -m "Add bar on main" &&
		but checkout -b submod-main
	) &&
	but add file1 "spaced name" file12 file14 file2 subdir/file3 submod &&
	but add both &&
	but rm file11 &&
	but cummit -m "main updates" &&

	but clean -fdx &&
	but checkout -b order-file-start main &&
	echo start >a &&
	echo start >b &&
	but add a b &&
	but cummit -m start &&
	but checkout -b order-file-side1 order-file-start &&
	echo side1 >a &&
	echo side1 >b &&
	but add a b &&
	but cummit -m side1 &&
	but checkout -b order-file-side2 order-file-start &&
	echo side2 >a &&
	echo side2 >b &&
	but add a b &&
	but cummit -m side2 &&

	but config merge.tool mytool &&
	but config mergetool.mytool.cmd "cat \"\$REMOTE\" >\"\$MERGED\"" &&
	but config mergetool.mytool.trustExitCode true &&
	but config mergetool.mybase.cmd "cat \"\$BASE\" >\"\$MERGED\"" &&
	but config mergetool.mybase.trustExitCode true
'

test_expect_success 'custom mergetool' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count branch1 &&
	but submodule update -N &&
	test_must_fail but merge main &&
	yes "" | but mergetool both &&
	yes "" | but mergetool file1 file1 &&
	yes "" | but mergetool file2 "spaced name" &&
	yes "" | but mergetool subdir/file3 &&
	yes "d" | but mergetool file11 &&
	yes "d" | but mergetool file12 &&
	yes "l" | but mergetool submod &&
	echo "main updated" >expect &&
	test_cmp expect file1 &&
	echo "main new" >expect &&
	test_cmp expect file2 &&
	echo "main new sub" >expect &&
	test_cmp expect subdir/file3 &&
	echo "branch1 submodule" >expect &&
	test_cmp expect submod/bar &&
	but cummit -m "branch1 resolved with mergetool"
'

test_expect_success 'gui mergetool' '
	test_config merge.guitool myguitool &&
	test_config mergetool.myguitool.cmd "(printf \"gui \" && cat \"\$REMOTE\") >\"\$MERGED\"" &&
	test_config mergetool.myguitool.trustExitCode true &&
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count branch1 &&
	but submodule update -N &&
	test_must_fail but merge main &&
	yes "" | but mergetool --gui both &&
	yes "" | but mergetool -g file1 file1 &&
	yes "" | but mergetool --gui file2 "spaced name" &&
	yes "" | but mergetool --gui subdir/file3 &&
	yes "d" | but mergetool --gui file11 &&
	yes "d" | but mergetool --gui file12 &&
	yes "l" | but mergetool --gui submod &&
	echo "gui main updated" >expect &&
	test_cmp expect file1 &&
	echo "gui main new" >expect &&
	test_cmp expect file2 &&
	echo "gui main new sub" >expect &&
	test_cmp expect subdir/file3 &&
	echo "branch1 submodule" >expect &&
	test_cmp expect submod/bar &&
	but cummit -m "branch1 resolved with mergetool"
'

test_expect_success 'gui mergetool without merge.guitool set falls back to merge.tool' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count branch1 &&
	but submodule update -N &&
	test_must_fail but merge main &&
	yes "" | but mergetool --gui both &&
	yes "" | but mergetool -g file1 file1 &&
	yes "" | but mergetool --gui file2 "spaced name" &&
	yes "" | but mergetool --gui subdir/file3 &&
	yes "d" | but mergetool --gui file11 &&
	yes "d" | but mergetool --gui file12 &&
	yes "l" | but mergetool --gui submod &&
	echo "main updated" >expect &&
	test_cmp expect file1 &&
	echo "main new" >expect &&
	test_cmp expect file2 &&
	echo "main new sub" >expect &&
	test_cmp expect subdir/file3 &&
	echo "branch1 submodule" >expect &&
	test_cmp expect submod/bar &&
	but cummit -m "branch1 resolved with mergetool"
'

test_expect_success 'mergetool crlf' '
	test_when_finished "but reset --hard" &&
	# This test_config line must go after the above reset line so that
	# core.autocrlf is unconfigured before reset runs.  (The
	# test_config command uses test_when_finished internally and
	# test_when_finished is LIFO.)
	test_config core.autocrlf true &&
	but checkout -b test$test_count branch1 &&
	test_must_fail but merge main &&
	yes "" | but mergetool file1 &&
	yes "" | but mergetool file2 &&
	yes "" | but mergetool "spaced name" &&
	yes "" | but mergetool both &&
	yes "" | but mergetool subdir/file3 &&
	yes "d" | but mergetool file11 &&
	yes "d" | but mergetool file12 &&
	yes "r" | but mergetool submod &&
	test "$(printf x | cat file1 -)" = "$(printf "main updated\r\nx")" &&
	test "$(printf x | cat file2 -)" = "$(printf "main new\r\nx")" &&
	test "$(printf x | cat subdir/file3 -)" = "$(printf "main new sub\r\nx")" &&
	but submodule update -N &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&
	but cummit -m "branch1 resolved with mergetool - autocrlf"
'

test_expect_success 'mergetool in subdir' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count branch1 &&
	but submodule update -N &&
	(
		cd subdir &&
		test_must_fail but merge main &&
		yes "" | but mergetool file3 &&
		echo "main new sub" >expect &&
		test_cmp expect file3
	)
'

test_expect_success 'mergetool on file in parent dir' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count branch1 &&
	but submodule update -N &&
	(
		cd subdir &&
		test_must_fail but merge main &&
		yes "" | but mergetool file3 &&
		yes "" | but mergetool ../file1 &&
		yes "" | but mergetool ../file2 ../spaced\ name &&
		yes "" | but mergetool ../both &&
		yes "d" | but mergetool ../file11 &&
		yes "d" | but mergetool ../file12 &&
		yes "l" | but mergetool ../submod &&
		echo "main updated" >expect &&
		test_cmp expect ../file1 &&
		echo "main new" >expect &&
		test_cmp expect ../file2 &&
		echo "branch1 submodule" >expect &&
		test_cmp expect ../submod/bar &&
		but cummit -m "branch1 resolved with mergetool - subdir"
	)
'

test_expect_success 'mergetool skips autoresolved' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count branch1 &&
	but submodule update -N &&
	test_must_fail but merge main &&
	test -n "$(but ls-files -u)" &&
	yes "d" | but mergetool file11 &&
	yes "d" | but mergetool file12 &&
	yes "l" | but mergetool submod &&
	output="$(but mergetool --no-prompt)" &&
	test "$output" = "No files need merging"
'

test_expect_success 'mergetool merges all from subdir (rerere disabled)' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count branch1 &&
	test_config rerere.enabled false &&
	(
		cd subdir &&
		test_must_fail but merge main &&
		yes "r" | but mergetool ../submod &&
		yes "d" "d" | but mergetool --no-prompt &&
		echo "main updated" >expect &&
		test_cmp expect ../file1 &&
		echo "main new" >expect &&
		test_cmp expect ../file2 &&
		echo "main new sub" >expect &&
		test_cmp expect file3 &&
		( cd .. && but submodule update -N ) &&
		echo "main submodule" >expect &&
		test_cmp expect ../submod/bar &&
		but cummit -m "branch2 resolved by mergetool from subdir"
	)
'

test_expect_success 'mergetool merges all from subdir (rerere enabled)' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count branch1 &&
	test_config rerere.enabled true &&
	rm -rf .but/rr-cache &&
	(
		cd subdir &&
		test_must_fail but merge main &&
		yes "r" | but mergetool ../submod &&
		yes "d" "d" | but mergetool --no-prompt &&
		echo "main updated" >expect &&
		test_cmp expect ../file1 &&
		echo "main new" >expect &&
		test_cmp expect ../file2 &&
		echo "main new sub" >expect &&
		test_cmp expect file3 &&
		( cd .. && but submodule update -N ) &&
		echo "main submodule" >expect &&
		test_cmp expect ../submod/bar &&
		but cummit -m "branch2 resolved by mergetool from subdir"
	)
'

test_expect_success 'mergetool skips resolved paths when rerere is active' '
	test_when_finished "but reset --hard" &&
	test_config rerere.enabled true &&
	rm -rf .but/rr-cache &&
	but checkout -b test$test_count branch1 &&
	but submodule update -N &&
	test_must_fail but merge main &&
	yes "l" | but mergetool --no-prompt submod &&
	yes "d" "d" | but mergetool --no-prompt &&
	but submodule update -N &&
	output="$(yes "n" | but mergetool --no-prompt)" &&
	test "$output" = "No files need merging"
'

test_expect_success 'conflicted stash sets up rerere'  '
	test_when_finished "but reset --hard" &&
	test_config rerere.enabled true &&
	but checkout stash1 &&
	echo "Conflicting stash content" >file11 &&
	but stash &&

	but checkout --detach stash2 &&
	test_must_fail but stash apply &&

	test -n "$(but ls-files -u)" &&
	conflicts="$(but rerere remaining)" &&
	test "$conflicts" = "file11" &&
	output="$(but mergetool --no-prompt)" &&
	test "$output" != "No files need merging" &&

	but cummit -am "save the stash resolution" &&

	but reset --hard stash2 &&
	test_must_fail but stash apply &&

	test -n "$(but ls-files -u)" &&
	conflicts="$(but rerere remaining)" &&
	test -z "$conflicts" &&
	output="$(but mergetool --no-prompt)" &&
	test "$output" = "No files need merging"
'

test_expect_success 'mergetool takes partial path' '
	test_when_finished "but reset --hard" &&
	test_config rerere.enabled false &&
	but checkout -b test$test_count branch1 &&
	but submodule update -N &&
	test_must_fail but merge main &&

	yes "" | but mergetool subdir &&

	echo "main new sub" >expect &&
	test_cmp expect subdir/file3
'

test_expect_success 'mergetool delete/delete conflict' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count move-to-c &&
	test_must_fail but merge move-to-b &&
	echo d | but mergetool a/a/file.txt &&
	! test -f a/a/file.txt &&
	but reset --hard &&
	test_must_fail but merge move-to-b &&
	echo m | but mergetool a/a/file.txt &&
	test -f b/b/file.txt &&
	but reset --hard &&
	test_must_fail but merge move-to-b &&
	! echo a | but mergetool a/a/file.txt &&
	! test -f a/a/file.txt
'

test_expect_success 'mergetool produces no errors when keepBackup is used' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count move-to-c &&
	test_config mergetool.keepBackup true &&
	test_must_fail but merge move-to-b &&
	echo d | but mergetool a/a/file.txt 2>actual &&
	test_must_be_empty actual &&
	! test -d a
'

test_expect_success 'mergetool honors tempfile config for deleted files' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count move-to-c &&
	test_config mergetool.keepTemporaries false &&
	test_must_fail but merge move-to-b &&
	echo d | but mergetool a/a/file.txt &&
	! test -d a
'

test_expect_success 'mergetool keeps tempfiles when aborting delete/delete' '
	test_when_finished "but reset --hard" &&
	test_when_finished "but clean -fdx" &&
	but checkout -b test$test_count move-to-c &&
	test_config mergetool.keepTemporaries true &&
	test_must_fail but merge move-to-b &&
	! test_write_lines a n | but mergetool a/a/file.txt &&
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
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count branch1 &&
	but submodule update -N &&
	mv submod submod-movedaside &&
	but rm --cached submod &&
	but cummit -m "Submodule deleted from branch" &&
	but checkout -b test$test_count.a test$test_count &&
	test_must_fail but merge main &&
	test -n "$(but ls-files -u)" &&
	yes "" | but mergetool file1 file2 spaced\ name subdir/file3 &&
	yes "" | but mergetool both &&
	yes "d" | but mergetool file11 file12 &&
	yes "r" | but mergetool submod &&
	rmdir submod && mv submod-movedaside submod &&
	echo "branch1 submodule" >expect &&
	test_cmp expect submod/bar &&
	but submodule update -N &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&
	output="$(but mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	but cummit -m "Merge resolved by keeping module" &&

	mv submod submod-movedaside &&
	but checkout -b test$test_count.b test$test_count &&
	but submodule update -N &&
	test_must_fail but merge main &&
	test -n "$(but ls-files -u)" &&
	yes "" | but mergetool file1 file2 spaced\ name subdir/file3 &&
	yes "" | but mergetool both &&
	yes "d" | but mergetool file11 file12 &&
	yes "l" | but mergetool submod &&
	test ! -e submod &&
	output="$(but mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	but cummit -m "Merge resolved by deleting module" &&

	mv submod-movedaside submod &&
	but checkout -b test$test_count.c main &&
	but submodule update -N &&
	test_must_fail but merge test$test_count &&
	test -n "$(but ls-files -u)" &&
	yes "" | but mergetool file1 file2 spaced\ name subdir/file3 &&
	yes "" | but mergetool both &&
	yes "d" | but mergetool file11 file12 &&
	yes "r" | but mergetool submod &&
	test ! -e submod &&
	test -d submod.orig &&
	but submodule update -N &&
	output="$(but mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	but cummit -m "Merge resolved by deleting module" &&
	mv submod.orig submod &&

	but checkout -b test$test_count.d main &&
	but submodule update -N &&
	test_must_fail but merge test$test_count &&
	test -n "$(but ls-files -u)" &&
	yes "" | but mergetool file1 file2 spaced\ name subdir/file3 &&
	yes "" | but mergetool both &&
	yes "d" | but mergetool file11 file12 &&
	yes "l" | but mergetool submod &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&
	but submodule update -N &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&
	output="$(but mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	but cummit -m "Merge resolved by keeping module"
'

test_expect_success 'file vs modified submodule' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count branch1 &&
	but submodule update -N &&
	mv submod submod-movedaside &&
	but rm --cached submod &&
	echo not a submodule >submod &&
	but add submod &&
	but cummit -m "Submodule path becomes file" &&
	but checkout -b test$test_count.a branch1 &&
	test_must_fail but merge main &&
	test -n "$(but ls-files -u)" &&
	yes "" | but mergetool file1 file2 spaced\ name subdir/file3 &&
	yes "" | but mergetool both &&
	yes "d" | but mergetool file11 file12 &&
	yes "r" | but mergetool submod &&
	rmdir submod && mv submod-movedaside submod &&
	echo "branch1 submodule" >expect &&
	test_cmp expect submod/bar &&
	but submodule update -N &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&
	output="$(but mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	but cummit -m "Merge resolved by keeping module" &&

	mv submod submod-movedaside &&
	but checkout -b test$test_count.b test$test_count &&
	test_must_fail but merge main &&
	test -n "$(but ls-files -u)" &&
	yes "" | but mergetool file1 file2 spaced\ name subdir/file3 &&
	yes "" | but mergetool both &&
	yes "d" | but mergetool file11 file12 &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		yes "c" | but mergetool submod~HEAD &&
		but rm submod &&
		but mv submod~HEAD submod
	else
		yes "l" | but mergetool submod
	fi &&
	but submodule update -N &&
	echo "not a submodule" >expect &&
	test_cmp expect submod &&
	output="$(but mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	but cummit -m "Merge resolved by keeping file" &&

	but checkout -b test$test_count.c main &&
	rmdir submod && mv submod-movedaside submod &&
	test ! -e submod.orig &&
	but submodule update -N &&
	test_must_fail but merge test$test_count &&
	test -n "$(but ls-files -u)" &&
	yes "" | but mergetool file1 file2 spaced\ name subdir/file3 &&
	yes "" | but mergetool both &&
	yes "d" | but mergetool file11 file12 &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		mv submod submod.orig &&
		but rm --cached submod &&
		yes "c" | but mergetool submod~test19 &&
		but mv submod~test19 submod
	else
		yes "r" | but mergetool submod
	fi &&
	test -d submod.orig &&
	but submodule update -N &&
	echo "not a submodule" >expect &&
	test_cmp expect submod &&
	output="$(but mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	but cummit -m "Merge resolved by keeping file" &&

	but checkout -b test$test_count.d main &&
	rmdir submod && mv submod.orig submod &&
	but submodule update -N &&
	test_must_fail but merge test$test_count &&
	test -n "$(but ls-files -u)" &&
	yes "" | but mergetool file1 file2 spaced\ name subdir/file3 &&
	yes "" | but mergetool both &&
	yes "d" | but mergetool file11 file12 &&
	yes "l" | but mergetool submod &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		yes "d" | but mergetool submod~test19
	fi &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&
	but submodule update -N &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&
	output="$(but mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	but cummit -m "Merge resolved by keeping module"
'

test_expect_success 'submodule in subdirectory' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count branch1 &&
	but submodule update -N &&
	(
		cd subdir &&
		test_create_repo subdir_module &&
		(
		cd subdir_module &&
		: >file15 &&
		but add file15 &&
		but cummit -m "add initial versions"
		)
	) &&
	test_when_finished "rm -rf subdir/subdir_module" &&
	but submodule add but://example.com/subsubmodule subdir/subdir_module &&
	but add subdir/subdir_module &&
	but cummit -m "add submodule in subdirectory" &&

	but checkout -b test$test_count.a test$test_count &&
	but submodule update -N &&
	(
	cd subdir/subdir_module &&
		but checkout -b super10.a &&
		echo test$test_count.a >file15 &&
		but add file15 &&
		but cummit -m "on branch 10.a"
	) &&
	but add subdir/subdir_module &&
	but cummit -m "change submodule in subdirectory on test$test_count.a" &&

	but checkout -b test$test_count.b test$test_count &&
	but submodule update -N &&
	(
		cd subdir/subdir_module &&
		but checkout -b super10.b &&
		echo test$test_count.b >file15 &&
		but add file15 &&
		but cummit -m "on branch 10.b"
	) &&
	but add subdir/subdir_module &&
	but cummit -m "change submodule in subdirectory on test$test_count.b" &&

	test_must_fail but merge test$test_count.a &&
	(
		cd subdir &&
		yes "l" | but mergetool subdir_module
	) &&
	echo "test$test_count.b" >expect &&
	test_cmp expect subdir/subdir_module/file15 &&
	but submodule update -N &&
	echo "test$test_count.b" >expect &&
	test_cmp expect subdir/subdir_module/file15 &&
	but reset --hard &&
	but submodule update -N &&

	test_must_fail but merge test$test_count.a &&
	yes "r" | but mergetool subdir/subdir_module &&
	echo "test$test_count.b" >expect &&
	test_cmp expect subdir/subdir_module/file15 &&
	but submodule update -N &&
	echo "test$test_count.a" >expect &&
	test_cmp expect subdir/subdir_module/file15 &&
	but cummit -m "branch1 resolved with mergetool"
'

test_expect_success 'directory vs modified submodule' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count branch1 &&
	mv submod submod-movedaside &&
	but rm --cached submod &&
	mkdir submod &&
	echo not a submodule >submod/file16 &&
	but add submod/file16 &&
	but cummit -m "Submodule path becomes directory" &&

	test_must_fail but merge main &&
	test -n "$(but ls-files -u)" &&
	yes "l" | but mergetool submod &&
	echo "not a submodule" >expect &&
	test_cmp expect submod/file16 &&
	rm -rf submod.orig &&

	but reset --hard &&
	test_must_fail but merge main &&
	test -n "$(but ls-files -u)" &&
	test ! -e submod.orig &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		yes "r" | but mergetool submod~main &&
		but mv submod submod.orig &&
		but mv submod~main submod
	else
		yes "r" | but mergetool submod
	fi &&
	test -d submod.orig &&
	echo "not a submodule" >expect &&
	test_cmp expect submod.orig/file16 &&
	rm -r submod.orig &&
	mv submod-movedaside/.but submod &&
	( cd submod && but clean -f && but reset --hard ) &&
	but submodule update -N &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&
	but reset --hard &&
	rm -rf submod-movedaside &&

	but checkout -b test$test_count.c main &&
	but submodule update -N &&
	test_must_fail but merge test$test_count &&
	test -n "$(but ls-files -u)" &&
	yes "l" | but mergetool submod &&
	but submodule update -N &&
	echo "main submodule" >expect &&
	test_cmp expect submod/bar &&

	but reset --hard &&
	but submodule update -N &&
	test_must_fail but merge test$test_count &&
	test -n "$(but ls-files -u)" &&
	test ! -e submod.orig &&
	yes "r" | but mergetool submod &&
	echo "not a submodule" >expect &&
	test_cmp expect submod/file16 &&

	but reset --hard main &&
	( cd submod && but clean -f && but reset --hard ) &&
	but submodule update -N
'

test_expect_success 'file with no base' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count branch1 &&
	test_must_fail but merge main &&
	but mergetool --no-prompt --tool mybase -- both &&
	test_must_be_empty both
'

test_expect_success 'custom commands override built-ins' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count branch1 &&
	test_config mergetool.defaults.cmd "cat \"\$REMOTE\" >\"\$MERGED\"" &&
	test_config mergetool.defaults.trustExitCode true &&
	test_must_fail but merge main &&
	but mergetool --no-prompt --tool defaults -- both &&
	echo main both added >expected &&
	test_cmp expected both
'

test_expect_success 'filenames seen by tools start with ./' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count branch1 &&
	test_config mergetool.writeToTemp false &&
	test_config mergetool.myecho.cmd "echo \"\$LOCAL\"" &&
	test_config mergetool.myecho.trustExitCode true &&
	test_must_fail but merge main &&
	but mergetool --no-prompt --tool myecho -- both >actual &&
	grep ^\./both_LOCAL_ actual
'

test_lazy_prereq MKTEMP '
	tempdir=$(mktemp -d -t foo.XXXXXX) &&
	test -d "$tempdir" &&
	rmdir "$tempdir"
'

test_expect_success MKTEMP 'temporary filenames are used with mergetool.writeToTemp' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count branch1 &&
	test_config mergetool.writeToTemp true &&
	test_config mergetool.myecho.cmd "echo \"\$LOCAL\"" &&
	test_config mergetool.myecho.trustExitCode true &&
	test_must_fail but merge main &&
	but mergetool --no-prompt --tool myecho -- both >actual &&
	! grep ^\./both_LOCAL_ actual &&
	grep /both_LOCAL_ actual
'

test_expect_success 'diff.orderFile configuration is honored' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count order-file-side2 &&
	test_config diff.orderFile order-file &&
	test_config mergetool.myecho.cmd "echo \"\$LOCAL\"" &&
	test_config mergetool.myecho.trustExitCode true &&
	echo b >order-file &&
	echo a >>order-file &&
	test_must_fail but merge order-file-side1 &&
	cat >expect <<-\EOF &&
		Merging:
		b
		a
	EOF

	# make sure "order-file" that is ambiguous between
	# rev and path is understood correctly.
	but branch order-file HEAD &&

	but mergetool --no-prompt --tool myecho >output &&
	but grep --no-index -h -A2 Merging: output >actual &&
	test_cmp expect actual
'
test_expect_success 'mergetool -Oorder-file is honored' '
	test_when_finished "but reset --hard" &&
	but checkout -b test$test_count order-file-side2 &&
	test_config diff.orderFile order-file &&
	test_config mergetool.myecho.cmd "echo \"\$LOCAL\"" &&
	test_config mergetool.myecho.trustExitCode true &&
	echo b >order-file &&
	echo a >>order-file &&
	test_must_fail but merge order-file-side1 &&
	cat >expect <<-\EOF &&
		Merging:
		a
		b
	EOF
	but mergetool -O/dev/null --no-prompt --tool myecho >output &&
	but grep --no-index -h -A2 Merging: output >actual &&
	test_cmp expect actual &&
	but reset --hard &&

	but config --unset diff.orderFile &&
	test_must_fail but merge order-file-side1 &&
	cat >expect <<-\EOF &&
		Merging:
		b
		a
	EOF
	but mergetool -Oorder-file --no-prompt --tool myecho >output &&
	but grep --no-index -h -A2 Merging: output >actual &&
	test_cmp expect actual
'

test_expect_success 'mergetool --tool-help shows recognized tools' '
	# Check a few known tools are correctly shown
	but mergetool --tool-help >mergetools &&
	grep vimdiff mergetools &&
	grep vimdiff3 mergetools &&
	grep gvimdiff2 mergetools &&
	grep araxis mergetools &&
	grep xxdiff mergetools &&
	grep meld mergetools
'

test_expect_success 'mergetool hideResolved' '
	test_config mergetool.hideResolved true &&
	test_when_finished "but reset --hard" &&
	but checkout -b test${test_count}_b main &&
	test_write_lines >file1 base "" a &&
	but cummit -a -m "base" &&
	test_write_lines >file1 base "" c &&
	but cummit -a -m "remote update" &&
	but checkout -b test${test_count}_a HEAD~ &&
	test_write_lines >file1 local "" b &&
	but cummit -a -m "local update" &&
	test_must_fail but merge test${test_count}_b &&
	yes "" | but mergetool file1 &&
	test_write_lines >expect local "" c &&
	test_cmp expect file1 &&
	but cummit -m "test resolved with mergetool"
'

test_done
