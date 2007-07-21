#!/bin/sh
#
# Copyright (c) 2007 Carlos Rica
#

test_description='git-stripspace'

. ./test-lib.sh

t40='A quick brown fox jumps over the lazy do'
s40='                                        '
sss="$s40$s40$s40$s40$s40$s40$s40$s40$s40$s40" # 400
ttt="$t40$t40$t40$t40$t40$t40$t40$t40$t40$t40" # 400

test_expect_success \
    'long lines without spaces should be unchanged' '
    echo "$ttt" >expect &&
    git-stripspace <expect >actual &&
    git diff expect actual &&

    echo "$ttt$ttt" >expect &&
    git-stripspace <expect >actual &&
    git diff expect actual &&

    echo "$ttt$ttt$ttt" >expect &&
    git-stripspace <expect >actual &&
    git diff expect actual &&

    echo "$ttt$ttt$ttt$ttt" >expect &&
    git-stripspace <expect >actual &&
    git diff expect actual
'

test_expect_success \
    'lines with spaces at the beginning should be unchanged' '
    echo "$sss$ttt" >expect &&
    git-stripspace <expect >actual &&
    git diff expect actual &&

    echo "$sss$sss$ttt" >expect &&
    git-stripspace <expect >actual &&
    git diff expect actual &&

    echo "$sss$sss$sss$ttt" >expect &&
    git-stripspace <expect >actual &&
    git diff expect actual
'

test_expect_success \
    'lines with intermediate spaces should be unchanged' '
    echo "$ttt$sss$ttt" >expect &&
    git-stripspace <expect >actual &&
    git diff expect actual &&

    echo "$ttt$sss$sss$ttt" >expect &&
    git-stripspace <expect >actual &&
    git diff expect actual
'

test_expect_success \
    'consecutive blank lines should be unified' '
    printf "$ttt\n\n$ttt\n" > expect &&
    printf "$ttt\n\n\n\n\n$ttt\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt$ttt\n\n$ttt\n" > expect &&
    printf "$ttt$ttt\n\n\n\n\n$ttt\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt$ttt$ttt\n\n$ttt\n" > expect &&
    printf "$ttt$ttt$ttt\n\n\n\n\n$ttt\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n\n$ttt\n" > expect &&
    printf "$ttt\n\n\n\n\n$ttt\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n\n$ttt$ttt\n" > expect &&
    printf "$ttt\n\n\n\n\n$ttt$ttt\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n\n$ttt$ttt$ttt\n" > expect &&
    printf "$ttt\n\n\n\n\n$ttt$ttt$ttt\n" | git-stripspace >actual &&
    git diff expect actual
'

test_expect_success \
    'consecutive blank lines at the beginning should be removed' '
    printf "" > expect &&
    printf "\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "" > expect &&
    printf "\n\n\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "" > expect &&
    printf "$sss\n$sss\n$sss\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "" > expect &&
    printf "$sss$sss\n$sss\n\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "" > expect &&
    printf "\n$sss\n$sss$sss\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "" > expect &&
    printf "$sss$sss$sss$sss\n\n\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "" > expect &&
    printf "\n$sss$sss$sss$sss\n\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "" > expect &&
    printf "\n\n$sss$sss$sss$sss\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n" > expect &&
    printf "\n$ttt\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n" > expect &&
    printf "\n\n\n$ttt\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt$ttt\n" > expect &&
    printf "\n\n\n$ttt$ttt\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt$ttt$ttt\n" > expect &&
    printf "\n\n\n$ttt$ttt$ttt\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt$ttt$ttt$ttt\n" > expect &&
    printf "\n\n\n$ttt$ttt$ttt$ttt\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n" > expect &&
    printf "$sss\n$sss\n$sss\n$ttt\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n" > expect &&
    printf "\n$sss\n$sss$sss\n$ttt\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n" > expect &&
    printf "$sss$sss\n$sss\n\n$ttt\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n" > expect &&
    printf "$sss$sss$sss\n\n\n$ttt\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n" > expect &&
    printf "\n$sss$sss$sss\n\n$ttt\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n" > expect &&
    printf "\n\n$sss$sss$sss\n$ttt\n" | git-stripspace >actual &&
    git diff expect actual
'

test_expect_success \
    'consecutive blank lines at the end should be removed' '
    printf "$ttt\n" > expect &&
    printf "$ttt\n\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n" > expect &&
    printf "$ttt\n\n\n\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt$ttt\n" > expect &&
    printf "$ttt$ttt\n\n\n\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt$ttt$ttt\n" > expect &&
    printf "$ttt$ttt$ttt\n\n\n\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt$ttt$ttt$ttt\n" > expect &&
    printf "$ttt$ttt$ttt$ttt\n\n\n\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n" > expect &&
    printf "$ttt\n$sss\n$sss\n$sss\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n" > expect &&
    printf "$ttt\n\n$sss\n$sss$sss\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n" > expect &&
    printf "$ttt\n$sss$sss\n$sss\n\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n" > expect &&
    printf "$ttt\n$sss$sss$sss\n\n\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n" > expect &&
    printf "$ttt\n\n$sss$sss$sss\n\n" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n" > expect &&
    printf "$ttt\n\n\n$sss$sss$sss\n" | git-stripspace >actual &&
    git diff expect actual
'

test_expect_success \
    'text without newline at end should end with newline' '
    test `printf "$ttt" | git-stripspace | wc -l` -gt 0 &&
    test `printf "$ttt$ttt" | git-stripspace | wc -l` -gt 0 &&
    test `printf "$ttt$ttt$ttt" | git-stripspace | wc -l` -gt 0 &&
    test `printf "$ttt$ttt$ttt$ttt" | git-stripspace | wc -l` -gt 0
'

# text plus spaces at the end:

test_expect_success \
    'text plus spaces without newline at end should end with newline' '
    test `printf "$ttt$sss" | git-stripspace | wc -l` -gt 0 &&
    test `printf "$ttt$ttt$sss" | git-stripspace | wc -l` -gt 0 &&
    test `printf "$ttt$ttt$ttt$sss" | git-stripspace | wc -l` -gt 0
    test `printf "$ttt$sss$sss" | git-stripspace | wc -l` -gt 0 &&
    test `printf "$ttt$ttt$sss$sss" | git-stripspace | wc -l` -gt 0 &&
    test `printf "$ttt$sss$sss$sss" | git-stripspace | wc -l` -gt 0
'

test_expect_failure \
    'text plus spaces without newline at end should not show spaces' '
    printf "$ttt$sss" | git-stripspace | grep -q "  " ||
    printf "$ttt$ttt$sss" | git-stripspace | grep -q "  " ||
    printf "$ttt$ttt$ttt$sss" | git-stripspace | grep -q "  " ||
    printf "$ttt$sss$sss" | git-stripspace | grep -q "  " ||
    printf "$ttt$ttt$sss$sss" | git-stripspace | grep -q "  " ||
    printf "$ttt$sss$sss$sss" | git-stripspace | grep -q "  "
'

test_expect_success \
    'text plus spaces without newline should show the correct lines' '
    printf "$ttt\n" >expect &&
    printf "$ttt$sss" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n" >expect &&
    printf "$ttt$sss$sss" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt\n" >expect &&
    printf "$ttt$sss$sss$sss" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt$ttt\n" >expect &&
    printf "$ttt$ttt$sss" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt$ttt\n" >expect &&
    printf "$ttt$ttt$sss$sss" | git-stripspace >actual &&
    git diff expect actual &&

    printf "$ttt$ttt$ttt\n" >expect &&
    printf "$ttt$ttt$ttt$sss" | git-stripspace >actual &&
    git diff expect actual
'

test_expect_failure \
    'text plus spaces at end should not show spaces' '
    echo "$ttt$sss" | git-stripspace | grep -q "  " ||
    echo "$ttt$ttt$sss" | git-stripspace | grep -q "  " ||
    echo "$ttt$ttt$ttt$sss" | git-stripspace | grep -q "  " ||
    echo "$ttt$sss$sss" | git-stripspace | grep -q "  " ||
    echo "$ttt$ttt$sss$sss" | git-stripspace | grep -q "  " ||
    echo "$ttt$sss$sss$sss" | git-stripspace | grep -q "  "
'

test_expect_success \
    'text plus spaces at end should be cleaned and newline must remain' '
    echo "$ttt" >expect &&
    echo "$ttt$sss" | git-stripspace >actual &&
    git diff expect actual &&

    echo "$ttt" >expect &&
    echo "$ttt$sss$sss" | git-stripspace >actual &&
    git diff expect actual &&

    echo "$ttt" >expect &&
    echo "$ttt$sss$sss$sss" | git-stripspace >actual &&
    git diff expect actual &&

    echo "$ttt$ttt" >expect &&
    echo "$ttt$ttt$sss" | git-stripspace >actual &&
    git diff expect actual &&

    echo "$ttt$ttt" >expect &&
    echo "$ttt$ttt$sss$sss" | git-stripspace >actual &&
    git diff expect actual &&

    echo "$ttt$ttt$ttt" >expect &&
    echo "$ttt$ttt$ttt$sss" | git-stripspace >actual &&
    git diff expect actual
'

# spaces only:

test_expect_success \
    'spaces with newline at end should be replaced with empty string' '
    printf "" >expect &&

    echo | git-stripspace >actual &&
    git diff expect actual &&

    echo "$sss" | git-stripspace >actual &&
    git diff expect actual &&

    echo "$sss$sss" | git-stripspace >actual &&
    git diff expect actual &&

    echo "$sss$sss$sss" | git-stripspace >actual &&
    git diff expect actual &&

    echo "$sss$sss$sss$sss" | git-stripspace >actual &&
    git diff expect actual
'

test_expect_failure \
    'spaces without newline at end should not show spaces' '
    printf "" | git-stripspace | grep -q " " ||
    printf "$sss" | git-stripspace | grep -q " " ||
    printf "$sss$sss" | git-stripspace | grep -q " " ||
    printf "$sss$sss$sss" | git-stripspace | grep -q " " ||
    printf "$sss$sss$sss$sss" | git-stripspace | grep -q " "
'

test_expect_success \
    'spaces without newline at end should be replaced with empty string' '
    printf "" >expect &&

    printf "" | git-stripspace >actual &&
    git diff expect actual

    printf "$sss$sss" | git-stripspace >actual &&
    git diff expect actual

    printf "$sss$sss$sss" | git-stripspace >actual &&
    git diff expect actual

    printf "$sss$sss$sss$sss" | git-stripspace >actual &&
    git diff expect actual
'

test_done
