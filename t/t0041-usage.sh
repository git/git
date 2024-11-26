#!/bin/sh

test_description='Test commands behavior when given invalid argument value'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup ' '
	test_commit "v1.0"
'

test_expect_success 'tag --contains <existent_tag>' '
	git tag --contains "v1.0" >actual 2>actual.err &&
	grep "v1.0" actual &&
	test_line_count = 0 actual.err
'

test_expect_success 'tag --contains <inexistent_tag>' '
	test_must_fail git tag --contains "notag" >actual 2>actual.err &&
	test_line_count = 0 actual &&
	test_grep "error" actual.err &&
	test_grep ! "usage" actual.err
'

test_expect_success 'tag --no-contains <existent_tag>' '
	git tag --no-contains "v1.0" >actual 2>actual.err  &&
	test_line_count = 0 actual &&
	test_line_count = 0 actual.err
'

test_expect_success 'tag --no-contains <inexistent_tag>' '
	test_must_fail git tag --no-contains "notag" >actual 2>actual.err &&
	test_line_count = 0 actual &&
	test_grep "error" actual.err &&
	test_grep ! "usage" actual.err
'

test_expect_success 'tag usage error' '
	test_must_fail git tag --noopt >actual 2>actual.err &&
	test_line_count = 0 actual &&
	test_grep "usage" actual.err
'

test_expect_success 'branch --contains <existent_commit>' '
	git branch --contains "main" >actual 2>actual.err &&
	test_grep "main" actual &&
	test_line_count = 0 actual.err
'

test_expect_success 'branch --contains <inexistent_commit>' '
	test_must_fail git branch --no-contains "nocommit" >actual 2>actual.err &&
	test_line_count = 0 actual &&
	test_grep "error" actual.err &&
	test_grep ! "usage" actual.err
'

test_expect_success 'branch --no-contains <existent_commit>' '
	git branch --no-contains "main" >actual 2>actual.err &&
	test_line_count = 0 actual &&
	test_line_count = 0 actual.err
'

test_expect_success 'branch --no-contains <inexistent_commit>' '
	test_must_fail git branch --no-contains "nocommit" >actual 2>actual.err &&
	test_line_count = 0 actual &&
	test_grep "error" actual.err &&
	test_grep ! "usage" actual.err
'

test_expect_success 'branch usage error' '
	test_must_fail git branch --noopt >actual 2>actual.err &&
	test_line_count = 0 actual &&
	test_grep "usage" actual.err
'

test_expect_success 'for-each-ref --contains <existent_object>' '
	git for-each-ref --contains "main" >actual 2>actual.err &&
	test_line_count = 2 actual &&
	test_line_count = 0 actual.err
'

test_expect_success 'for-each-ref --contains <inexistent_object>' '
	test_must_fail git for-each-ref --no-contains "noobject" >actual 2>actual.err &&
	test_line_count = 0 actual &&
	test_grep "error" actual.err &&
	test_grep ! "usage" actual.err
'

test_expect_success 'for-each-ref --no-contains <existent_object>' '
	git for-each-ref --no-contains "main" >actual 2>actual.err &&
	test_line_count = 0 actual &&
	test_line_count = 0 actual.err
'

test_expect_success 'for-each-ref --no-contains <inexistent_object>' '
	test_must_fail git for-each-ref --no-contains "noobject" >actual 2>actual.err &&
	test_line_count = 0 actual &&
	test_grep "error" actual.err &&
	test_grep ! "usage" actual.err
'

test_expect_success 'for-each-ref usage error' '
	test_must_fail git for-each-ref --noopt >actual 2>actual.err &&
	test_line_count = 0 actual &&
	test_grep "usage" actual.err
'

test_done
