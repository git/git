#!/bin/sh

test_description='read-tree -u --reset'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-read-tree.sh

# two-tree test

test_expect_success 'setup' '
	but init &&
	mkdir df &&
	echo content >df/file &&
	but add df/file &&
	but cummit -m one &&
	but ls-files >expect &&
	rm -rf df &&
	echo content >df &&
	but add df &&
	echo content >new &&
	but add new &&
	but cummit -m two
'

test_expect_success 'reset should work' '
	read_tree_u_must_succeed -u --reset HEAD^ &&
	but ls-files >actual &&
	test_cmp expect actual
'

test_expect_success 'reset should remove remnants from a failed merge' '
	read_tree_u_must_succeed --reset -u HEAD &&
	but ls-files -s >expect &&
	sha1=$(but rev-parse :new) &&
	(
		echo "100644 $sha1 1	old" &&
		echo "100644 $sha1 3	old"
	) | but update-index --index-info &&
	>old &&
	but ls-files -s &&
	read_tree_u_must_succeed --reset -u HEAD &&
	but ls-files -s >actual &&
	! test -f old
'

test_expect_success 'two-way reset should remove remnants too' '
	read_tree_u_must_succeed --reset -u HEAD &&
	but ls-files -s >expect &&
	sha1=$(but rev-parse :new) &&
	(
		echo "100644 $sha1 1	old" &&
		echo "100644 $sha1 3	old"
	) | but update-index --index-info &&
	>old &&
	but ls-files -s &&
	read_tree_u_must_succeed --reset -u HEAD HEAD &&
	but ls-files -s >actual &&
	! test -f old
'

test_expect_success 'Porcelain reset should remove remnants too' '
	read_tree_u_must_succeed --reset -u HEAD &&
	but ls-files -s >expect &&
	sha1=$(but rev-parse :new) &&
	(
		echo "100644 $sha1 1	old" &&
		echo "100644 $sha1 3	old"
	) | but update-index --index-info &&
	>old &&
	but ls-files -s &&
	but reset --hard &&
	but ls-files -s >actual &&
	! test -f old
'

test_expect_success 'Porcelain checkout -f should remove remnants too' '
	read_tree_u_must_succeed --reset -u HEAD &&
	but ls-files -s >expect &&
	sha1=$(but rev-parse :new) &&
	(
		echo "100644 $sha1 1	old" &&
		echo "100644 $sha1 3	old"
	) | but update-index --index-info &&
	>old &&
	but ls-files -s &&
	but checkout -f &&
	but ls-files -s >actual &&
	! test -f old
'

test_expect_success 'Porcelain checkout -f HEAD should remove remnants too' '
	read_tree_u_must_succeed --reset -u HEAD &&
	but ls-files -s >expect &&
	sha1=$(but rev-parse :new) &&
	(
		echo "100644 $sha1 1	old" &&
		echo "100644 $sha1 3	old"
	) | but update-index --index-info &&
	>old &&
	but ls-files -s &&
	but checkout -f HEAD &&
	but ls-files -s >actual &&
	! test -f old
'

test_done
