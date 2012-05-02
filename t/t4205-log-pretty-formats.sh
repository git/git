#!/bin/sh
#
# Copyright (c) 2010, Will Palmer
#

test_description='Test pretty formats'
. ./test-lib.sh

test_expect_success 'set up basic repos' '
	>foo &&
	>bar &&
	git add foo &&
	test_tick &&
	git commit -m initial &&
	git add bar &&
	test_tick &&
	git commit -m "add bar"
'

test_expect_success 'alias builtin format' '
	git log --pretty=oneline >expected &&
	git config pretty.test-alias oneline &&
	git log --pretty=test-alias >actual &&
	test_cmp expected actual
'

test_expect_success 'alias masking builtin format' '
	git log --pretty=oneline >expected &&
	git config pretty.oneline "%H" &&
	git log --pretty=oneline >actual &&
	test_cmp expected actual
'

test_expect_success 'alias user-defined format' '
	git log --pretty="format:%h" >expected &&
	git config pretty.test-alias "format:%h" &&
	git log --pretty=test-alias >actual &&
	test_cmp expected actual
'

test_expect_success 'alias user-defined tformat' '
	git log --pretty="tformat:%h" >expected &&
	git config pretty.test-alias "tformat:%h" &&
	git log --pretty=test-alias >actual &&
	test_cmp expected actual
'

test_expect_success 'alias non-existent format' '
	git config pretty.test-alias format-that-will-never-exist &&
	test_must_fail git log --pretty=test-alias
'

test_expect_success 'alias of an alias' '
	git log --pretty="tformat:%h" >expected &&
	git config pretty.test-foo "tformat:%h" &&
	git config pretty.test-bar test-foo &&
	git log --pretty=test-bar >actual && test_cmp expected actual
'

test_expect_success 'alias masking an alias' '
	git log --pretty=format:"Two %H" >expected &&
	git config pretty.duplicate "format:One %H" &&
	git config --add pretty.duplicate "format:Two %H" &&
	git log --pretty=duplicate >actual &&
	test_cmp expected actual
'

test_expect_success 'alias loop' '
	git config pretty.test-foo test-bar &&
	git config pretty.test-bar test-foo &&
	test_must_fail git log --pretty=test-foo
'

test_expect_success 'NUL separation' '
	printf "add bar\0initial" >expected &&
	git log -z --pretty="format:%s" >actual &&
	test_cmp expected actual
'

test_expect_success 'NUL termination' '
	printf "add bar\0initial\0" >expected &&
	git log -z --pretty="tformat:%s" >actual &&
	test_cmp expected actual
'

test_expect_success 'NUL separation with --stat' '
	stat0_part=$(git diff --stat HEAD^ HEAD) &&
	stat1_part=$(git diff --stat --root HEAD^) &&
	printf "add bar\n$stat0_part\n\0initial\n$stat1_part\n" >expected &&
	git log -z --stat --pretty="format:%s" >actual &&
	test_cmp expected actual
'

test_expect_failure 'NUL termination with --stat' '
	stat0_part=$(git diff --stat HEAD^ HEAD) &&
	stat1_part=$(git diff --stat --root HEAD^) &&
	printf "add bar\n$stat0_part\n\0initial\n$stat1_part\n\0" >expected &&
	git log -z --stat --pretty="tformat:%s" >actual &&
	test_cmp expected actual
'

test_done
