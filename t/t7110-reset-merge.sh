#!/bin/sh
#
# Copyright (c) 2009 Christian Couder
#

test_description='Tests for "git reset --merge"'

. ./test-lib.sh

test_expect_success setup '
    for i in 1 2 3; do echo line $i; done >file1 &&
    cat file1 >file2 &&
    git add file1 file2 &&
    test_tick &&
    git commit -m "Initial commit" &&
    git tag initial &&
    echo line 4 >>file1 &&
    cat file1 >file2 &&
    test_tick &&
    git commit -m "add line 4 to file1" file1 &&
    git tag second
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     C       C     C    D     --merge  D       D     D
# file2:     C       D     D    D     --merge  C       D     D
test_expect_success 'reset --merge is ok with changes in file it does not touch' '
    git reset --merge HEAD^ &&
    ! grep 4 file1 &&
    grep 4 file2 &&
    test "$(git rev-parse HEAD)" = "$(git rev-parse initial)" &&
    test -z "$(git diff --cached)"
'

test_expect_success 'reset --merge is ok when switching back' '
    git reset --merge second &&
    grep 4 file1 &&
    grep 4 file2 &&
    test "$(git rev-parse HEAD)" = "$(git rev-parse second)" &&
    test -z "$(git diff --cached)"
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     B       B     C    D     --merge  D       D     D
# file2:     C       D     D    D     --merge  C       D     D
test_expect_success 'reset --merge discards changes added to index (1)' '
    git reset --hard second &&
    cat file1 >file2 &&
    echo "line 5" >> file1 &&
    git add file1 &&
    git reset --merge HEAD^ &&
    ! grep 4 file1 &&
    ! grep 5 file1 &&
    grep 4 file2 &&
    test "$(git rev-parse HEAD)" = "$(git rev-parse initial)" &&
    test -z "$(git diff --cached)"
'

test_expect_success 'reset --merge is ok again when switching back (1)' '
    git reset --hard initial &&
    echo "line 5" >> file2 &&
    git add file2 &&
    git reset --merge second &&
    ! grep 4 file2 &&
    ! grep 5 file1 &&
    grep 4 file1 &&
    test "$(git rev-parse HEAD)" = "$(git rev-parse second)" &&
    test -z "$(git diff --cached)"
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     C       C     C    D     --merge  D       D     D
# file2:     C       C     D    D     --merge  D       D     D
test_expect_success 'reset --merge discards changes added to index (2)' '
    git reset --hard second &&
    echo "line 4" >> file2 &&
    git add file2 &&
    git reset --merge HEAD^ &&
    ! grep 4 file2 &&
    test "$(git rev-parse HEAD)" = "$(git rev-parse initial)" &&
    test -z "$(git diff)" &&
    test -z "$(git diff --cached)"
'

test_expect_success 'reset --merge is ok again when switching back (2)' '
    git reset --hard initial &&
    git reset --merge second &&
    ! grep 4 file2 &&
    grep 4 file1 &&
    test "$(git rev-parse HEAD)" = "$(git rev-parse second)" &&
    test -z "$(git diff --cached)"
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     A       B     B    C     --merge  (disallowed)
test_expect_success 'reset --merge fails with changes in file it touches' '
    git reset --hard second &&
    echo "line 5" >> file1 &&
    test_tick &&
    git commit -m "add line 5" file1 &&
    sed -e "s/line 1/changed line 1/" <file1 >file3 &&
    mv file3 file1 &&
    test_must_fail git reset --merge HEAD^ 2>err.log &&
    grep file1 err.log | grep "not uptodate"
'

test_expect_success 'setup 2 different branches' '
    git reset --hard second &&
    git branch branch1 &&
    git branch branch2 &&
    git checkout branch1 &&
    echo "line 5 in branch1" >> file1 &&
    test_tick &&
    git commit -a -m "change in branch1" &&
    git checkout branch2 &&
    echo "line 5 in branch2" >> file1 &&
    test_tick &&
    git commit -a -m "change in branch2" &&
    git tag third
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     X       U     B    C     --merge  (disallowed)
test_expect_success '"reset --merge HEAD^" fails with pending merge' '
    test_must_fail git merge branch1 &&
    test_must_fail git reset --merge HEAD^ &&
    test "$(git rev-parse HEAD)" = "$(git rev-parse third)" &&
    test -n "$(git diff --cached)"
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     X       U     B    B     --merge  (disallowed)
test_expect_success '"reset --merge HEAD" fails with pending merge' '
    git reset --hard third &&
    test_must_fail git merge branch1 &&
    test_must_fail git reset --merge HEAD &&
    test "$(git rev-parse HEAD)" = "$(git rev-parse third)" &&
    test -n "$(git diff --cached)"
'

test_done
