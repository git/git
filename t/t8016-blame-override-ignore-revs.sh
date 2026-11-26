#!/bin/sh

test_description='default revisions to ignore when blaming'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'blame: override-ignore-revs' '
    test_commit first-commit hello.txt hello &&

    echo world >>hello.txt &&
    test_commit second-commit hello.txt &&

    sed "1s/hello/hi/" <hello.txt > hello.txt.tmp &&
    mv hello.txt.tmp hello.txt &&
    test_commit third-commit hello.txt &&

    git blame hello.txt >expect &&
    git rev-parse HEAD >.git-blame-ignore-revs &&
    git blame -O hello.txt >actual &&

    test_cmp expect actual
'

test_done
