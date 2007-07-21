#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git-apply --stat --summary test.

'
. ./test-lib.sh

test_expect_success \
    'rename' \
    'git-apply --stat --summary <../t4100/t-apply-1.patch >current &&
    git diff ../t4100/t-apply-1.expect current'

test_expect_success \
    'copy' \
    'git-apply --stat --summary <../t4100/t-apply-2.patch >current &&
    git diff ../t4100/t-apply-2.expect current'

test_expect_success \
    'rewrite' \
    'git-apply --stat --summary <../t4100/t-apply-3.patch >current &&
    git diff ../t4100/t-apply-3.expect current'

test_expect_success \
    'mode' \
    'git-apply --stat --summary <../t4100/t-apply-4.patch >current &&
    git diff ../t4100/t-apply-4.expect current'

test_expect_success \
    'non git' \
    'git-apply --stat --summary <../t4100/t-apply-5.patch >current &&
    git diff ../t4100/t-apply-5.expect current'

test_expect_success \
    'non git' \
    'git-apply --stat --summary <../t4100/t-apply-6.patch >current &&
    git diff ../t4100/t-apply-6.expect current'

test_expect_success \
    'non git' \
    'git-apply --stat --summary <../t4100/t-apply-7.patch >current &&
    git diff ../t4100/t-apply-7.expect current'

test_done
