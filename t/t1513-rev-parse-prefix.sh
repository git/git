#!/bin/sh

test_description='Tests for rev-parse --prefix'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	mkdir -p sub1/sub2 &&
	echo top >top &&
	echo file1 >sub1/file1 &&
	echo file2 >sub1/sub2/file2 &&
	git add top sub1/file1 sub1/sub2/file2 &&
	git commit -m commit
'

test_expect_success 'empty prefix -- file' '
	git rev-parse --prefix "" -- top sub1/file1 >actual &&
	cat <<-\EOF >expected &&
	--
	top
	sub1/file1
	EOF
	test_cmp expected actual
'

test_expect_success 'valid prefix -- file' '
	git rev-parse --prefix sub1/ -- file1 sub2/file2 >actual &&
	cat <<-\EOF >expected &&
	--
	sub1/file1
	sub1/sub2/file2
	EOF
	test_cmp expected actual
'

test_expect_success 'valid prefix -- ../file' '
	git rev-parse --prefix sub1/ -- ../top sub2/file2 >actual &&
	cat <<-\EOF >expected &&
	--
	sub1/../top
	sub1/sub2/file2
	EOF
	test_cmp expected actual
'

test_expect_success 'empty prefix HEAD:./path' '
	git rev-parse --prefix "" HEAD:./top >actual &&
	git rev-parse HEAD:top >expected &&
	test_cmp expected actual
'

test_expect_success 'valid prefix HEAD:./path' '
	git rev-parse --prefix sub1/ HEAD:./file1 >actual &&
	git rev-parse HEAD:sub1/file1 >expected &&
	test_cmp expected actual
'

test_expect_success 'valid prefix HEAD:../path' '
	git rev-parse --prefix sub1/ HEAD:../top >actual &&
	git rev-parse HEAD:top >expected &&
	test_cmp expected actual
'

test_expect_success 'prefix ignored with HEAD:top' '
	git rev-parse --prefix sub1/ HEAD:top >actual &&
	git rev-parse HEAD:top >expected &&
	test_cmp expected actual
'

test_expect_success 'disambiguate path with valid prefix' '
	git rev-parse --prefix sub1/ file1 >actual &&
	cat <<-\EOF >expected &&
	sub1/file1
	EOF
	test_cmp expected actual
'

test_expect_success 'file and refs with prefix' '
	git rev-parse --prefix sub1/ main file1 >actual &&
	cat <<-EOF >expected &&
	$(git rev-parse main)
	sub1/file1
	EOF
	test_cmp expected actual
'

test_expect_success 'two-levels deep' '
	git rev-parse --prefix sub1/sub2/ -- file2 >actual &&
	cat <<-\EOF >expected &&
	--
	sub1/sub2/file2
	EOF
	test_cmp expected actual
'

test_done
