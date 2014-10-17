#!/bin/sh
#
# Copyright (c) 2008 Charles Bailey
#

test_description='git mergetool

Testing basic merge tool invocation'

. ./test-lib.sh

# All the mergetool test work by checking out a temporary branch based
# off 'branch1' and then merging in master and checking the results of
# running mergetool

test_expect_success 'setup' '
	test_config rerere.enabled true &&
	echo master >file1 &&
	echo master spaced >"spaced name" &&
	echo master file11 >file11 &&
	echo master file12 >file12 &&
	echo master file13 >file13 &&
	echo master file14 >file14 &&
	mkdir subdir &&
	echo master sub >subdir/file3 &&
	test_create_repo submod &&
	(
		cd submod &&
		: >foo &&
		git add foo &&
		git commit -m "Add foo"
	) &&
	git submodule add git://example.com/submod submod &&
	git add file1 "spaced name" file1[1-4] subdir/file3 .gitmodules submod &&
	git commit -m "add initial versions" &&

	git checkout -b branch1 master &&
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

	git checkout -b stash1 master &&
	echo stash1 change file11 >file11 &&
	git add file11 &&
	git commit -m "stash1 changes" &&

	git checkout -b stash2 master &&
	echo stash2 change file11 >file11 &&
	git add file11 &&
	git commit -m "stash2 changes" &&

	git checkout master &&
	git submodule update -N &&
	echo master updated >file1 &&
	echo master new >file2 &&
	echo master updated spaced >"spaced name" &&
	echo master both added >both &&
	echo master updated file12 >file12 &&
	echo master updated file14 >file14 &&
	echo master new sub >subdir/file3 &&
	(
		cd submod &&
		echo master submodule >bar &&
		git add bar &&
		git commit -m "Add bar on master" &&
		git checkout -b submod-master
	) &&
	git add file1 "spaced name" file12 file14 file2 subdir/file3 submod &&
	git add both &&
	git rm file11 &&
	git commit -m "master updates" &&

	git config merge.tool mytool &&
	git config mergetool.mytool.cmd "cat \"\$REMOTE\" >\"\$MERGED\"" &&
	git config mergetool.mytool.trustExitCode true &&
	git config mergetool.mybase.cmd "cat \"\$BASE\" >\"\$MERGED\"" &&
	git config mergetool.mybase.trustExitCode true
'

test_expect_success 'custom mergetool' '
	git checkout -b test1 branch1 &&
	git submodule update -N &&
	test_must_fail git merge master >/dev/null 2>&1 &&
	( yes "" | git mergetool both >/dev/null 2>&1 ) &&
	( yes "" | git mergetool file1 file1 ) &&
	( yes "" | git mergetool file2 "spaced name" >/dev/null 2>&1 ) &&
	( yes "" | git mergetool subdir/file3 >/dev/null 2>&1 ) &&
	( yes "d" | git mergetool file11 >/dev/null 2>&1 ) &&
	( yes "d" | git mergetool file12 >/dev/null 2>&1 ) &&
	( yes "l" | git mergetool submod >/dev/null 2>&1 ) &&
	test "$(cat file1)" = "master updated" &&
	test "$(cat file2)" = "master new" &&
	test "$(cat subdir/file3)" = "master new sub" &&
	test "$(cat submod/bar)" = "branch1 submodule" &&
	git commit -m "branch1 resolved with mergetool"
'

test_expect_success 'mergetool crlf' '
	test_config core.autocrlf true &&
	git checkout -b test2 branch1 &&
	test_must_fail git merge master >/dev/null 2>&1 &&
	( yes "" | git mergetool file1 >/dev/null 2>&1 ) &&
	( yes "" | git mergetool file2 >/dev/null 2>&1 ) &&
	( yes "" | git mergetool "spaced name" >/dev/null 2>&1 ) &&
	( yes "" | git mergetool both >/dev/null 2>&1 ) &&
	( yes "" | git mergetool subdir/file3 >/dev/null 2>&1 ) &&
	( yes "d" | git mergetool file11 >/dev/null 2>&1 ) &&
	( yes "d" | git mergetool file12 >/dev/null 2>&1 ) &&
	( yes "r" | git mergetool submod >/dev/null 2>&1 ) &&
	test "$(printf x | cat file1 -)" = "$(printf "master updated\r\nx")" &&
	test "$(printf x | cat file2 -)" = "$(printf "master new\r\nx")" &&
	test "$(printf x | cat subdir/file3 -)" = "$(printf "master new sub\r\nx")" &&
	git submodule update -N &&
	test "$(cat submod/bar)" = "master submodule" &&
	git commit -m "branch1 resolved with mergetool - autocrlf" &&
	test_config core.autocrlf false &&
	git reset --hard
'

test_expect_success 'mergetool in subdir' '
	git checkout -b test3 branch1 &&
	git submodule update -N &&
	(
		cd subdir &&
		test_must_fail git merge master >/dev/null 2>&1 &&
		( yes "" | git mergetool file3 >/dev/null 2>&1 ) &&
		test "$(cat file3)" = "master new sub"
	)
'

test_expect_success 'mergetool on file in parent dir' '
	(
		cd subdir &&
		( yes "" | git mergetool ../file1 >/dev/null 2>&1 ) &&
		( yes "" | git mergetool ../file2 ../spaced\ name >/dev/null 2>&1 ) &&
		( yes "" | git mergetool ../both >/dev/null 2>&1 ) &&
		( yes "d" | git mergetool ../file11 >/dev/null 2>&1 ) &&
		( yes "d" | git mergetool ../file12 >/dev/null 2>&1 ) &&
		( yes "l" | git mergetool ../submod >/dev/null 2>&1 ) &&
		test "$(cat ../file1)" = "master updated" &&
		test "$(cat ../file2)" = "master new" &&
		test "$(cat ../submod/bar)" = "branch1 submodule" &&
		git commit -m "branch1 resolved with mergetool - subdir"
	)
'

test_expect_success 'mergetool skips autoresolved' '
	git checkout -b test4 branch1 &&
	git submodule update -N &&
	test_must_fail git merge master &&
	test -n "$(git ls-files -u)" &&
	( yes "d" | git mergetool file11 >/dev/null 2>&1 ) &&
	( yes "d" | git mergetool file12 >/dev/null 2>&1 ) &&
	( yes "l" | git mergetool submod >/dev/null 2>&1 ) &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git reset --hard
'

test_expect_success 'mergetool merges all from subdir' '
	(
		cd subdir &&
		test_config rerere.enabled false &&
		test_must_fail git merge master &&
		( yes "r" | git mergetool ../submod ) &&
		( yes "d" "d" | git mergetool --no-prompt ) &&
		test "$(cat ../file1)" = "master updated" &&
		test "$(cat ../file2)" = "master new" &&
		test "$(cat file3)" = "master new sub" &&
		( cd .. && git submodule update -N ) &&
		test "$(cat ../submod/bar)" = "master submodule" &&
		git commit -m "branch2 resolved by mergetool from subdir"
	)
'

test_expect_success 'mergetool skips resolved paths when rerere is active' '
	test_config rerere.enabled true &&
	rm -rf .git/rr-cache &&
	git checkout -b test5 branch1 &&
	git submodule update -N &&
	test_must_fail git merge master >/dev/null 2>&1 &&
	( yes "l" | git mergetool --no-prompt submod >/dev/null 2>&1 ) &&
	( yes "d" "d" | git mergetool --no-prompt >/dev/null 2>&1 ) &&
	git submodule update -N &&
	output="$(yes "n" | git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git reset --hard
'

test_expect_success 'conflicted stash sets up rerere'  '
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
	git reset --hard &&
	test_config rerere.enabled false &&
	git checkout -b test12 branch1 &&
	git submodule update -N &&
	test_must_fail git merge master &&

	( yes "" | git mergetool subdir ) &&

	test "$(cat subdir/file3)" = "master new sub" &&
	git reset --hard
'

test_expect_success 'deleted vs modified submodule' '
	git checkout -b test6 branch1 &&
	git submodule update -N &&
	mv submod submod-movedaside &&
	git rm --cached submod &&
	git commit -m "Submodule deleted from branch" &&
	git checkout -b test6.a test6 &&
	test_must_fail git merge master &&
	test -n "$(git ls-files -u)" &&
	( yes "" | git mergetool file1 file2 spaced\ name subdir/file3 >/dev/null 2>&1 ) &&
	( yes "" | git mergetool both >/dev/null 2>&1 ) &&
	( yes "d" | git mergetool file11 file12 >/dev/null 2>&1 ) &&
	( yes "r" | git mergetool submod ) &&
	rmdir submod && mv submod-movedaside submod &&
	test "$(cat submod/bar)" = "branch1 submodule" &&
	git submodule update -N &&
	test "$(cat submod/bar)" = "master submodule" &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git commit -m "Merge resolved by keeping module" &&

	mv submod submod-movedaside &&
	git checkout -b test6.b test6 &&
	git submodule update -N &&
	test_must_fail git merge master &&
	test -n "$(git ls-files -u)" &&
	( yes "" | git mergetool file1 file2 spaced\ name subdir/file3 >/dev/null 2>&1 ) &&
	( yes "" | git mergetool both >/dev/null 2>&1 ) &&
	( yes "d" | git mergetool file11 file12 >/dev/null 2>&1 ) &&
	( yes "l" | git mergetool submod ) &&
	test ! -e submod &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git commit -m "Merge resolved by deleting module" &&

	mv submod-movedaside submod &&
	git checkout -b test6.c master &&
	git submodule update -N &&
	test_must_fail git merge test6 &&
	test -n "$(git ls-files -u)" &&
	( yes "" | git mergetool file1 file2 spaced\ name subdir/file3 >/dev/null 2>&1 ) &&
	( yes "" | git mergetool both >/dev/null 2>&1 ) &&
	( yes "d" | git mergetool file11 file12 >/dev/null 2>&1 ) &&
	( yes "r" | git mergetool submod ) &&
	test ! -e submod &&
	test -d submod.orig &&
	git submodule update -N &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git commit -m "Merge resolved by deleting module" &&
	mv submod.orig submod &&

	git checkout -b test6.d master &&
	git submodule update -N &&
	test_must_fail git merge test6 &&
	test -n "$(git ls-files -u)" &&
	( yes "" | git mergetool file1 file2 spaced\ name subdir/file3 >/dev/null 2>&1 ) &&
	( yes "" | git mergetool both >/dev/null 2>&1 ) &&
	( yes "d" | git mergetool file11 file12 >/dev/null 2>&1 ) &&
	( yes "l" | git mergetool submod ) &&
	test "$(cat submod/bar)" = "master submodule" &&
	git submodule update -N &&
	test "$(cat submod/bar)" = "master submodule" &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git commit -m "Merge resolved by keeping module" &&
	git reset --hard HEAD
'

test_expect_success 'file vs modified submodule' '
	git checkout -b test7 branch1 &&
	git submodule update -N &&
	mv submod submod-movedaside &&
	git rm --cached submod &&
	echo not a submodule >submod &&
	git add submod &&
	git commit -m "Submodule path becomes file" &&
	git checkout -b test7.a branch1 &&
	test_must_fail git merge master &&
	test -n "$(git ls-files -u)" &&
	( yes "" | git mergetool file1 file2 spaced\ name subdir/file3 >/dev/null 2>&1 ) &&
	( yes "" | git mergetool both >/dev/null 2>&1 ) &&
	( yes "d" | git mergetool file11 file12 >/dev/null 2>&1 ) &&
	( yes "r" | git mergetool submod ) &&
	rmdir submod && mv submod-movedaside submod &&
	test "$(cat submod/bar)" = "branch1 submodule" &&
	git submodule update -N &&
	test "$(cat submod/bar)" = "master submodule" &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git commit -m "Merge resolved by keeping module" &&

	mv submod submod-movedaside &&
	git checkout -b test7.b test7 &&
	test_must_fail git merge master &&
	test -n "$(git ls-files -u)" &&
	( yes "" | git mergetool file1 file2 spaced\ name subdir/file3 >/dev/null 2>&1 ) &&
	( yes "" | git mergetool both >/dev/null 2>&1 ) &&
	( yes "d" | git mergetool file11 file12 >/dev/null 2>&1 ) &&
	( yes "l" | git mergetool submod ) &&
	git submodule update -N &&
	test "$(cat submod)" = "not a submodule" &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git commit -m "Merge resolved by keeping file" &&

	git checkout -b test7.c master &&
	rmdir submod && mv submod-movedaside submod &&
	test ! -e submod.orig &&
	git submodule update -N &&
	test_must_fail git merge test7 &&
	test -n "$(git ls-files -u)" &&
	( yes "" | git mergetool file1 file2 spaced\ name subdir/file3 >/dev/null 2>&1 ) &&
	( yes "" | git mergetool both >/dev/null 2>&1 ) &&
	( yes "d" | git mergetool file11 file12 >/dev/null 2>&1 ) &&
	( yes "r" | git mergetool submod ) &&
	test -d submod.orig &&
	git submodule update -N &&
	test "$(cat submod)" = "not a submodule" &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git commit -m "Merge resolved by keeping file" &&

	git checkout -b test7.d master &&
	rmdir submod && mv submod.orig submod &&
	git submodule update -N &&
	test_must_fail git merge test7 &&
	test -n "$(git ls-files -u)" &&
	( yes "" | git mergetool file1 file2 spaced\ name subdir/file3 >/dev/null 2>&1 ) &&
	( yes "" | git mergetool both>/dev/null 2>&1 ) &&
	( yes "d" | git mergetool file11 file12 >/dev/null 2>&1 ) &&
	( yes "l" | git mergetool submod ) &&
	test "$(cat submod/bar)" = "master submodule" &&
	git submodule update -N &&
	test "$(cat submod/bar)" = "master submodule" &&
	output="$(git mergetool --no-prompt)" &&
	test "$output" = "No files need merging" &&
	git commit -m "Merge resolved by keeping module"
'

test_expect_success 'submodule in subdirectory' '
	git checkout -b test10 branch1 &&
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
	git submodule add git://example.com/subsubmodule subdir/subdir_module &&
	git add subdir/subdir_module &&
	git commit -m "add submodule in subdirectory" &&

	git checkout -b test10.a test10 &&
	git submodule update -N &&
	(
	cd subdir/subdir_module &&
		git checkout -b super10.a &&
		echo test10.a >file15 &&
		git add file15 &&
		git commit -m "on branch 10.a"
	) &&
	git add subdir/subdir_module &&
	git commit -m "change submodule in subdirectory on test10.a" &&

	git checkout -b test10.b test10 &&
	git submodule update -N &&
	(
		cd subdir/subdir_module &&
		git checkout -b super10.b &&
		echo test10.b >file15 &&
		git add file15 &&
		git commit -m "on branch 10.b"
	) &&
	git add subdir/subdir_module &&
	git commit -m "change submodule in subdirectory on test10.b" &&

	test_must_fail git merge test10.a >/dev/null 2>&1 &&
	(
		cd subdir &&
		( yes "l" | git mergetool subdir_module )
	) &&
	test "$(cat subdir/subdir_module/file15)" = "test10.b" &&
	git submodule update -N &&
	test "$(cat subdir/subdir_module/file15)" = "test10.b" &&
	git reset --hard &&
	git submodule update -N &&

	test_must_fail git merge test10.a >/dev/null 2>&1 &&
	( yes "r" | git mergetool subdir/subdir_module ) &&
	test "$(cat subdir/subdir_module/file15)" = "test10.b" &&
	git submodule update -N &&
	test "$(cat subdir/subdir_module/file15)" = "test10.a" &&
	git commit -m "branch1 resolved with mergetool" &&
	rm -rf subdir/subdir_module
'

test_expect_success 'directory vs modified submodule' '
	git checkout -b test11 branch1 &&
	mv submod submod-movedaside &&
	git rm --cached submod &&
	mkdir submod &&
	echo not a submodule >submod/file16 &&
	git add submod/file16 &&
	git commit -m "Submodule path becomes directory" &&

	test_must_fail git merge master &&
	test -n "$(git ls-files -u)" &&
	( yes "l" | git mergetool submod ) &&
	test "$(cat submod/file16)" = "not a submodule" &&
	rm -rf submod.orig &&

	git reset --hard >/dev/null 2>&1 &&
	test_must_fail git merge master &&
	test -n "$(git ls-files -u)" &&
	test ! -e submod.orig &&
	( yes "r" | git mergetool submod ) &&
	test -d submod.orig &&
	test "$(cat submod.orig/file16)" = "not a submodule" &&
	rm -r submod.orig &&
	mv submod-movedaside/.git submod &&
	( cd submod && git clean -f && git reset --hard ) &&
	git submodule update -N &&
	test "$(cat submod/bar)" = "master submodule" &&
	git reset --hard >/dev/null 2>&1 && rm -rf submod-movedaside &&

	git checkout -b test11.c master &&
	git submodule update -N &&
	test_must_fail git merge test11 &&
	test -n "$(git ls-files -u)" &&
	( yes "l" | git mergetool submod ) &&
	git submodule update -N &&
	test "$(cat submod/bar)" = "master submodule" &&

	git reset --hard >/dev/null 2>&1 &&
	git submodule update -N &&
	test_must_fail git merge test11 &&
	test -n "$(git ls-files -u)" &&
	test ! -e submod.orig &&
	( yes "r" | git mergetool submod ) &&
	test "$(cat submod/file16)" = "not a submodule" &&

	git reset --hard master >/dev/null 2>&1 &&
	( cd submod && git clean -f && git reset --hard ) &&
	git submodule update -N
'

test_expect_success 'file with no base' '
	git checkout -b test13 branch1 &&
	test_must_fail git merge master &&
	git mergetool --no-prompt --tool mybase -- both &&
	>expected &&
	test_cmp both expected &&
	git reset --hard master >/dev/null 2>&1
'

test_expect_success 'custom commands override built-ins' '
	git checkout -b test14 branch1 &&
	test_config mergetool.defaults.cmd "cat \"\$REMOTE\" >\"\$MERGED\"" &&
	test_config mergetool.defaults.trustExitCode true &&
	test_must_fail git merge master &&
	git mergetool --no-prompt --tool defaults -- both &&
	echo master both added >expected &&
	test_cmp both expected &&
	git reset --hard master >/dev/null 2>&1
'

test_expect_success 'filenames seen by tools start with ./' '
	git checkout -b test15 branch1 &&
	test_config mergetool.writeToTemp false &&
	test_config mergetool.myecho.cmd "echo \"\$LOCAL\"" &&
	test_config mergetool.myecho.trustExitCode true &&
	test_must_fail git merge master &&
	git mergetool --no-prompt --tool myecho -- both >actual &&
	grep ^\./both_LOCAL_ actual >/dev/null &&
	git reset --hard master >/dev/null 2>&1
'

test_expect_success 'temporary filenames are used with mergetool.writeToTemp' '
	git checkout -b test16 branch1 &&
	test_config mergetool.writeToTemp true &&
	test_config mergetool.myecho.cmd "echo \"\$LOCAL\"" &&
	test_config mergetool.myecho.trustExitCode true &&
	test_must_fail git merge master &&
	git mergetool --no-prompt --tool myecho -- both >actual &&
	test_must_fail grep ^\./both_LOCAL_ actual >/dev/null &&
	grep /both_LOCAL_ actual >/dev/null &&
	git reset --hard master >/dev/null 2>&1
'

test_done
