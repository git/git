#!/bin/sh

test_description='git annotate'
. ./test-lib.sh

PROG='git annotate'
. "$TEST_DIRECTORY"/annotate-tests.sh

test_expect_success \
    'Annotating an old revision works' \
    '[ $(git annotate file master | awk "{print \$3}" | grep -c "^A$") -eq 2 ] && \
     [ $(git annotate file master | awk "{print \$3}" | grep -c "^B$") -eq 2 ]'


test_done
