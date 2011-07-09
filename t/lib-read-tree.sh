#!/bin/sh
#
# Helper functions to check if read-tree would succeed/fail as expected with
# and without the dry-run option. They also test that the dry-run does not
# write the index and that together with -u it doesn't touch the work tree.
#
read_tree_must_succeed () {
    git ls-files -s >pre-dry-run &&
    git read-tree -n "$@" &&
    git ls-files -s >post-dry-run &&
    test_cmp pre-dry-run post-dry-run &&
    git read-tree "$@"
}

read_tree_must_fail () {
    git ls-files -s >pre-dry-run &&
    test_must_fail git read-tree -n "$@" &&
    git ls-files -s >post-dry-run &&
    test_cmp pre-dry-run post-dry-run &&
    test_must_fail git read-tree "$@"
}

read_tree_u_must_succeed () {
    git ls-files -s >pre-dry-run &&
    git diff-files -p >pre-dry-run-wt &&
    git read-tree -n "$@" &&
    git ls-files -s >post-dry-run &&
    git diff-files -p >post-dry-run-wt &&
    test_cmp pre-dry-run post-dry-run &&
    test_cmp pre-dry-run-wt post-dry-run-wt &&
    git read-tree "$@"
}

read_tree_u_must_fail () {
    git ls-files -s >pre-dry-run &&
    git diff-files -p >pre-dry-run-wt &&
    test_must_fail git read-tree -n "$@" &&
    git ls-files -s >post-dry-run &&
    git diff-files -p >post-dry-run-wt &&
    test_cmp pre-dry-run post-dry-run &&
    test_cmp pre-dry-run-wt post-dry-run-wt &&
    test_must_fail git read-tree "$@"
}
