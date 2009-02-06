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
    echo master >file1 &&
    mkdir subdir &&
    echo master sub >subdir/file3 &&
    git add file1 subdir/file3 &&
    git commit -m "added file1" &&

    git checkout -b branch1 master &&
    echo branch1 change >file1 &&
    echo branch1 newfile >file2 &&
    echo branch1 sub >subdir/file3 &&
    git add file1 file2 subdir/file3 &&
    git commit -m "branch1 changes" &&

    git checkout master &&
    echo master updated >file1 &&
    echo master new >file2 &&
    echo master new sub >subdir/file3 &&
    git add file1 file2 subdir/file3 &&
    git commit -m "master updates" &&

    git config merge.tool mytool &&
    git config mergetool.mytool.cmd "cat \"\$REMOTE\" >\"\$MERGED\"" &&
    git config mergetool.mytool.trustExitCode true
'

test_expect_success 'custom mergetool' '
    git checkout -b test1 branch1 &&
    test_must_fail git merge master >/dev/null 2>&1 &&
    ( yes "" | git mergetool file1 >/dev/null 2>&1 ) &&
    ( yes "" | git mergetool file2 >/dev/null 2>&1 ) &&
    ( yes "" | git mergetool subdir/file3 >/dev/null 2>&1 ) &&
    test "$(cat file1)" = "master updated" &&
    test "$(cat file2)" = "master new" &&
    test "$(cat subdir/file3)" = "master new sub" &&
    git commit -m "branch1 resolved with mergetool"
'

test_expect_success 'mergetool crlf' '
    git config core.autocrlf true &&
    git checkout -b test2 branch1
    test_must_fail git merge master >/dev/null 2>&1 &&
    ( yes "" | git mergetool file1 >/dev/null 2>&1 ) &&
    ( yes "" | git mergetool file2 >/dev/null 2>&1 ) &&
    ( yes "" | git mergetool subdir/file3 >/dev/null 2>&1 ) &&
    test "$(printf x | cat file1 -)" = "$(printf "master updated\r\nx")" &&
    test "$(printf x | cat file2 -)" = "$(printf "master new\r\nx")" &&
    test "$(printf x | cat subdir/file3 -)" = "$(printf "master new sub\r\nx")" &&
    git commit -m "branch1 resolved with mergetool - autocrlf" &&
    git config core.autocrlf false &&
    git reset --hard
'

test_expect_success 'mergetool in subdir' '
    git checkout -b test3 branch1
    cd subdir && (
    test_must_fail git merge master >/dev/null 2>&1 &&
    ( yes "" | git mergetool file3 >/dev/null 2>&1 ) &&
    test "$(cat file3)" = "master new sub" )
'

# We can't merge files from parent directories when running mergetool
# from a subdir. Is this a bug?
#
#test_expect_failure 'mergetool in subdir' '
#    cd subdir && (
#    ( yes "" | git mergetool ../file1 >/dev/null 2>&1 ) &&
#    ( yes "" | git mergetool ../file2 >/dev/null 2>&1 ) &&
#    test "$(cat ../file1)" = "master updated" &&
#    test "$(cat ../file2)" = "master new" &&
#    git commit -m "branch1 resolved with mergetool - subdir" )
#'

test_done
