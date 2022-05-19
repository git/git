#!/bin/sh
#
# Copyright (c) 2009 Christian Couder
#

test_description='Tests for "but reset" with "--merge" and "--keep" options'

. ./test-lib.sh

test_expect_success setup '
    printf "line %d\n" 1 2 3 >file1 &&
    cat file1 >file2 &&
    but add file1 file2 &&
    test_tick &&
    but cummit -m "Initial cummit" &&
    but tag initial &&
    echo line 4 >>file1 &&
    cat file1 >file2 &&
    test_tick &&
    but cummit -m "add line 4 to file1" file1 &&
    but tag second
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     C       C     C    D     --merge  D       D     D
# file2:     C       D     D    D     --merge  C       D     D
test_expect_success 'reset --merge is ok with changes in file it does not touch' '
    but reset --merge HEAD^ &&
    ! grep 4 file1 &&
    grep 4 file2 &&
    test "$(but rev-parse HEAD)" = "$(but rev-parse initial)" &&
    test -z "$(but diff --cached)"
'

test_expect_success 'reset --merge is ok when switching back' '
    but reset --merge second &&
    grep 4 file1 &&
    grep 4 file2 &&
    test "$(but rev-parse HEAD)" = "$(but rev-parse second)" &&
    test -z "$(but diff --cached)"
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     C       C     C    D     --keep   D       D     D
# file2:     C       D     D    D     --keep   C       D     D
test_expect_success 'reset --keep is ok with changes in file it does not touch' '
    but reset --hard second &&
    cat file1 >file2 &&
    but reset --keep HEAD^ &&
    ! grep 4 file1 &&
    grep 4 file2 &&
    test "$(but rev-parse HEAD)" = "$(but rev-parse initial)" &&
    test -z "$(but diff --cached)"
'

test_expect_success 'reset --keep is ok when switching back' '
    but reset --keep second &&
    grep 4 file1 &&
    grep 4 file2 &&
    test "$(but rev-parse HEAD)" = "$(but rev-parse second)" &&
    test -z "$(but diff --cached)"
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     B       B     C    D     --merge  D       D     D
# file2:     C       D     D    D     --merge  C       D     D
test_expect_success 'reset --merge discards changes added to index (1)' '
    but reset --hard second &&
    cat file1 >file2 &&
    echo "line 5" >> file1 &&
    but add file1 &&
    but reset --merge HEAD^ &&
    ! grep 4 file1 &&
    ! grep 5 file1 &&
    grep 4 file2 &&
    test "$(but rev-parse HEAD)" = "$(but rev-parse initial)" &&
    test -z "$(but diff --cached)"
'

test_expect_success 'reset --merge is ok again when switching back (1)' '
    but reset --hard initial &&
    echo "line 5" >> file2 &&
    but add file2 &&
    but reset --merge second &&
    ! grep 4 file2 &&
    ! grep 5 file1 &&
    grep 4 file1 &&
    test "$(but rev-parse HEAD)" = "$(but rev-parse second)" &&
    test -z "$(but diff --cached)"
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     B       B     C    D     --keep   (disallowed)
test_expect_success 'reset --keep fails with changes in index in files it touches' '
    but reset --hard second &&
    echo "line 5" >> file1 &&
    but add file1 &&
    test_must_fail but reset --keep HEAD^
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     C       C     C    D     --merge  D       D     D
# file2:     C       C     D    D     --merge  D       D     D
test_expect_success 'reset --merge discards changes added to index (2)' '
    but reset --hard second &&
    echo "line 4" >> file2 &&
    but add file2 &&
    but reset --merge HEAD^ &&
    ! grep 4 file2 &&
    test "$(but rev-parse HEAD)" = "$(but rev-parse initial)" &&
    test -z "$(but diff)" &&
    test -z "$(but diff --cached)"
'

test_expect_success 'reset --merge is ok again when switching back (2)' '
    but reset --hard initial &&
    but reset --merge second &&
    ! grep 4 file2 &&
    grep 4 file1 &&
    test "$(but rev-parse HEAD)" = "$(but rev-parse second)" &&
    test -z "$(but diff --cached)"
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     C       C     C    D     --keep   D       D     D
# file2:     C       C     D    D     --keep   C       D     D
test_expect_success 'reset --keep keeps changes it does not touch' '
    but reset --hard second &&
    echo "line 4" >> file2 &&
    but add file2 &&
    but reset --keep HEAD^ &&
    grep 4 file2 &&
    test "$(but rev-parse HEAD)" = "$(but rev-parse initial)" &&
    test -z "$(but diff --cached)"
'

test_expect_success 'reset --keep keeps changes when switching back' '
    but reset --keep second &&
    grep 4 file2 &&
    grep 4 file1 &&
    test "$(but rev-parse HEAD)" = "$(but rev-parse second)" &&
    test -z "$(but diff --cached)"
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     A       B     B    C     --merge  (disallowed)
test_expect_success 'reset --merge fails with changes in file it touches' '
    but reset --hard second &&
    echo "line 5" >> file1 &&
    test_tick &&
    but cummit -m "add line 5" file1 &&
    sed -e "s/line 1/changed line 1/" <file1 >file3 &&
    mv file3 file1 &&
    test_must_fail but reset --merge HEAD^ 2>err.log &&
    grep file1 err.log | grep "not uptodate"
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     A       B     B    C     --keep   (disallowed)
test_expect_success 'reset --keep fails with changes in file it touches' '
    but reset --hard second &&
    echo "line 5" >> file1 &&
    test_tick &&
    but cummit -m "add line 5" file1 &&
    sed -e "s/line 1/changed line 1/" <file1 >file3 &&
    mv file3 file1 &&
    test_must_fail but reset --keep HEAD^ 2>err.log &&
    grep file1 err.log | grep "not uptodate"
'

test_expect_success 'setup 3 different branches' '
    but reset --hard second &&
    but branch branch1 &&
    but branch branch2 &&
    but branch branch3 &&
    but checkout branch1 &&
    echo "line 5 in branch1" >> file1 &&
    test_tick &&
    but cummit -a -m "change in branch1" &&
    but checkout branch2 &&
    echo "line 5 in branch2" >> file1 &&
    test_tick &&
    but cummit -a -m "change in branch2" &&
    but tag third &&
    but checkout branch3 &&
    echo a new file >file3 &&
    rm -f file1 &&
    but add file3 &&
    test_tick &&
    but cummit -a -m "change in branch3"
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     X       U     B    C     --merge  C       C     C
test_expect_success '"reset --merge HEAD^" is ok with pending merge' '
    but checkout third &&
    test_must_fail but merge branch1 &&
    but reset --merge HEAD^ &&
    test "$(but rev-parse HEAD)" = "$(but rev-parse second)" &&
    test -z "$(but diff --cached)" &&
    test -z "$(but diff)"
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     X       U     B    C     --keep   (disallowed)
test_expect_success '"reset --keep HEAD^" fails with pending merge' '
    but reset --hard third &&
    test_must_fail but merge branch1 &&
    test_must_fail but reset --keep HEAD^ 2>err.log &&
    test_i18ngrep "middle of a merge" err.log
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     X       U     B    B     --merge  B       B     B
test_expect_success '"reset --merge HEAD" is ok with pending merge' '
    but reset --hard third &&
    test_must_fail but merge branch1 &&
    but reset --merge HEAD &&
    test "$(but rev-parse HEAD)" = "$(but rev-parse third)" &&
    test -z "$(but diff --cached)" &&
    test -z "$(but diff)"
'

# The next test will test the following:
#
#           working index HEAD target         working index HEAD
#           ----------------------------------------------------
# file1:     X       U     B    B     --keep   (disallowed)
test_expect_success '"reset --keep HEAD" fails with pending merge' '
    but reset --hard third &&
    test_must_fail but merge branch1 &&
    test_must_fail but reset --keep HEAD 2>err.log &&
    test_i18ngrep "middle of a merge" err.log
'

test_expect_success '--merge is ok with added/deleted merge' '
    but reset --hard third &&
    rm -f file2 &&
    test_must_fail but merge branch3 &&
    ! test -f file2 &&
    test -f file3 &&
    but diff --exit-code file3 &&
    but diff --exit-code branch3 file3 &&
    but reset --merge HEAD &&
    ! test -f file3 &&
    ! test -f file2 &&
    but diff --exit-code --cached
'

test_expect_success '--keep fails with added/deleted merge' '
    but reset --hard third &&
    rm -f file2 &&
    test_must_fail but merge branch3 &&
    ! test -f file2 &&
    test -f file3 &&
    but diff --exit-code file3 &&
    but diff --exit-code branch3 file3 &&
    test_must_fail but reset --keep HEAD 2>err.log &&
    test_i18ngrep "middle of a merge" err.log
'

test_done
