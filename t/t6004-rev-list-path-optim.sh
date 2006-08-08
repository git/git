#!/bin/sh

test_description='git-rev-list trivial path optimization test'

. ./test-lib.sh

test_expect_success setup '
echo Hello > a &&
git add a &&
git commit -m "Initial commit" a
'

test_expect_success path-optimization '
    commit=$(echo "Unchanged tree" | git-commit-tree "HEAD^{tree}" -p HEAD) &&
    test $(git-rev-list $commit | wc -l) = 2 &&
    test $(git-rev-list $commit -- . | wc -l) = 1
'

test_done
