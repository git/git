#!/bin/sh

test_description='ls-tree --format'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'ls-tree --format usage' '
	test_expect_code 129 git ls-tree --format=fmt -l HEAD &&
	test_expect_code 129 git ls-tree --format=fmt --name-only HEAD &&
	test_expect_code 129 git ls-tree --format=fmt --name-status HEAD &&
	test_expect_code 129 git ls-tree --format=fmt --object-only HEAD
'

test_expect_success 'setup' '
	mkdir dir &&
	test_commit dir/sub-file &&
	test_commit top-file
'

test_ls_tree_format () {
	format=$1 &&
	opts=$2 &&
	shift 2 &&
	git ls-tree $opts -r HEAD >expect.raw &&
	sed "s/^/> /" >expect <expect.raw &&
	git ls-tree --format="> $format" -r HEAD >actual &&
	test_cmp expect actual
}

test_expect_success 'ls-tree --format=<default-like>' '
	test_ls_tree_format \
		"%(mode) %(type) %(object)%x09%(file)" \
		""
'

test_expect_success 'ls-tree --format=<long-like>' '
	test_ls_tree_format \
		"%(mode) %(type) %(object) %(size:padded)%x09%(file)" \
		"--long"
'

test_expect_success 'ls-tree --format=<name-only-like>' '
	test_ls_tree_format \
		"%(file)" \
		"--name-only"
'

test_expect_success 'ls-tree --format=<object-only-like>' '
	test_ls_tree_format \
		"%(object)" \
		"--object-only"
'

test_done
