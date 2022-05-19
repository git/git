# Helper functions to check if read-tree would succeed/fail as expected with
# and without the dry-run option. They also test that the dry-run does not
# write the index and that together with -u it doesn't touch the work tree.
#
read_tree_must_succeed () {
	but ls-files -s >pre-dry-run &&
	but read-tree -n "$@" &&
	but ls-files -s >post-dry-run &&
	test_cmp pre-dry-run post-dry-run &&
	but read-tree "$@"
}

read_tree_must_fail () {
	but ls-files -s >pre-dry-run &&
	test_must_fail but read-tree -n "$@" &&
	but ls-files -s >post-dry-run &&
	test_cmp pre-dry-run post-dry-run &&
	test_must_fail but read-tree "$@"
}

read_tree_u_must_succeed () {
	but ls-files -s >pre-dry-run &&
	but diff-files -p >pre-dry-run-wt &&
	but read-tree -n "$@" &&
	but ls-files -s >post-dry-run &&
	but diff-files -p >post-dry-run-wt &&
	test_cmp pre-dry-run post-dry-run &&
	test_cmp pre-dry-run-wt post-dry-run-wt &&
	but read-tree "$@"
}

read_tree_u_must_fail () {
	but ls-files -s >pre-dry-run &&
	but diff-files -p >pre-dry-run-wt &&
	test_must_fail but read-tree -n "$@" &&
	but ls-files -s >post-dry-run &&
	but diff-files -p >post-dry-run-wt &&
	test_cmp pre-dry-run post-dry-run &&
	test_cmp pre-dry-run-wt post-dry-run-wt &&
	test_must_fail but read-tree "$@"
}
