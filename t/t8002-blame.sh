#!/bin/sh

test_description='git blame'
. ./test-lib.sh

PROG='git blame -c'
. "$TEST_DIRECTORY"/annotate-tests.sh

PROG='git blame -c -e'
test_expect_success 'Blame --show-email works' '
    check_count "<A@test.git>" 1 "<B@test.git>" 1 "<B1@test.git>" 1 "<B2@test.git>" 1 "<author@example.com>" 1 "<C@test.git>" 1 "<D@test.git>" 1 "<E at test dot git>" 1
'

test_done
