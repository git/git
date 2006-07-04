#!/bin/sh

test_description='git-annotate'
. ./test-lib.sh

PROG='git annotate'
. ../annotate-tests.sh

test_expect_success \
    'Annotating an old revision works' \
    '[ $(git annotate file master | awk "{print \$3}" | grep -c "^A$") == 2 ] && \
     [ $(git annotate file master | awk "{print \$3}" | grep -c "^B$") == 2 ]'


test_done
