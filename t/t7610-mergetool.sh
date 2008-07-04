#!/bin/sh
#
# Copyright (c) 2008 Charles Bailey
#

test_description='git-mergetool

Testing basic merge tool invocation'

. ./test-lib.sh

test_expect_success 'setup' '
    echo master >file1 &&
    git add file1 &&
    git commit -m "added file1" &&
    git checkout -b branch1 master &&
    echo branch1 change >file1 &&
    echo branch1 newfile >file2 &&
    git add file1 file2 &&
    git commit -m "branch1 changes" &&
    git checkout -b branch2 master &&
    echo branch2 change >file1 &&
    echo branch2 newfile >file2 &&
    git add file1 file2 &&
    git commit -m "branch2 changes" &&
    git checkout master &&
    echo master updated >file1 &&
    echo master new >file2 &&
    git add file1 file2 &&
    git commit -m "master updates"
'

test_expect_success 'custom mergetool' '
    git config merge.tool mytool &&
    git config mergetool.mytool.cmd "cat \"\$REMOTE\" >\"\$MERGED\"" &&
    git config mergetool.mytool.trustExitCode true &&
	git checkout branch1 &&
    ! git merge master >/dev/null 2>&1 &&
    ( yes "" | git mergetool file1>/dev/null 2>&1 ) &&
    ( yes "" | git mergetool file2>/dev/null 2>&1 ) &&
    test "$(cat file1)" = "master updated" &&
    test "$(cat file2)" = "master new" &&
	git commit -m "branch1 resolved with mergetool"
'

test_done
