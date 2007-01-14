#!/bin/sh

test_description='git-rev-list --max-count and --skip test'

. ./test-lib.sh

test_expect_success 'setup' '
    for n in 1 2 3 4 5 ; do \
        echo $n > a ; \
        git add a ; \
        git commit -m "$n" ; \
    done
'

test_expect_success 'no options' '
    test $(git-rev-list HEAD | wc -l) = 5
'

test_expect_success '--max-count' '
    test $(git-rev-list HEAD --max-count=0 | wc -l) = 0 &&
    test $(git-rev-list HEAD --max-count=3 | wc -l) = 3 &&
    test $(git-rev-list HEAD --max-count=5 | wc -l) = 5 &&
    test $(git-rev-list HEAD --max-count=10 | wc -l) = 5
'

test_expect_success '--max-count all forms' '
    test $(git-rev-list HEAD --max-count=1 | wc -l) = 1 &&
    test $(git-rev-list HEAD -1 | wc -l) = 1 &&
    test $(git-rev-list HEAD -n1 | wc -l) = 1 &&
    test $(git-rev-list HEAD -n 1 | wc -l) = 1
'

test_expect_success '--skip' '
    test $(git-rev-list HEAD --skip=0 | wc -l) = 5 &&
    test $(git-rev-list HEAD --skip=3 | wc -l) = 2 &&
    test $(git-rev-list HEAD --skip=5 | wc -l) = 0 &&
    test $(git-rev-list HEAD --skip=10 | wc -l) = 0
'

test_expect_success '--skip --max-count' '
    test $(git-rev-list HEAD --skip=0 --max-count=0 | wc -l) = 0 &&
    test $(git-rev-list HEAD --skip=0 --max-count=10 | wc -l) = 5 &&
    test $(git-rev-list HEAD --skip=3 --max-count=0 | wc -l) = 0 &&
    test $(git-rev-list HEAD --skip=3 --max-count=1 | wc -l) = 1 &&
    test $(git-rev-list HEAD --skip=3 --max-count=2 | wc -l) = 2 &&
    test $(git-rev-list HEAD --skip=3 --max-count=10 | wc -l) = 2 &&
    test $(git-rev-list HEAD --skip=5 --max-count=10 | wc -l) = 0 &&
    test $(git-rev-list HEAD --skip=10 --max-count=10 | wc -l) = 0
'

test_done
