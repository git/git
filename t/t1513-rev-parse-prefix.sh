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
	but add top sub1/file1 sub1/sub2/file2 &&
	but cummit -m cummit
'

test_expect_success 'empty prefix -- file' '
	but rev-parse --prefix "" -- top sub1/file1 >actual &&
	cat <<-\EOF >expected &&
	--
	top
	sub1/file1
	EOF
	test_cmp expected actual
'

test_expect_success 'valid prefix -- file' '
	but rev-parse --prefix sub1/ -- file1 sub2/file2 >actual &&
	cat <<-\EOF >expected &&
	--
	sub1/file1
	sub1/sub2/file2
	EOF
	test_cmp expected actual
'

test_expect_success 'valid prefix -- ../file' '
	but rev-parse --prefix sub1/ -- ../top sub2/file2 >actual &&
	cat <<-\EOF >expected &&
	--
	sub1/../top
	sub1/sub2/file2
	EOF
	test_cmp expected actual
'

test_expect_success 'empty prefix HEAD:./path' '
	but rev-parse --prefix "" HEAD:./top >actual &&
	but rev-parse HEAD:top >expected &&
	test_cmp expected actual
'

test_expect_success 'valid prefix HEAD:./path' '
	but rev-parse --prefix sub1/ HEAD:./file1 >actual &&
	but rev-parse HEAD:sub1/file1 >expected &&
	test_cmp expected actual
'

test_expect_success 'valid prefix HEAD:../path' '
	but rev-parse --prefix sub1/ HEAD:../top >actual &&
	but rev-parse HEAD:top >expected &&
	test_cmp expected actual
'

test_expect_success 'prefix ignored with HEAD:top' '
	but rev-parse --prefix sub1/ HEAD:top >actual &&
	but rev-parse HEAD:top >expected &&
	test_cmp expected actual
'

test_expect_success 'disambiguate path with valid prefix' '
	but rev-parse --prefix sub1/ file1 >actual &&
	cat <<-\EOF >expected &&
	sub1/file1
	EOF
	test_cmp expected actual
'

test_expect_success 'file and refs with prefix' '
	but rev-parse --prefix sub1/ main file1 >actual &&
	cat <<-EOF >expected &&
	$(but rev-parse main)
	sub1/file1
	EOF
	test_cmp expected actual
'

test_expect_success 'two-levels deep' '
	but rev-parse --prefix sub1/sub2/ -- file2 >actual &&
	cat <<-\EOF >expected &&
	--
	sub1/sub2/file2
	EOF
	test_cmp expected actual
'

test_done
