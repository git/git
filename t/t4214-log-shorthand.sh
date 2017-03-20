#!/bin/sh

test_description='log can show previous branch using shorthand - for @{-1}'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit first &&
	test_commit second &&
	test_commit third &&
	test_commit fourth &&
	test_commit fifth &&
	test_commit sixth &&
	test_commit seventh
'

test_expect_success '"log -" should not work initially' '
	test_must_fail git log -
'

test_expect_success 'setup branches for testing' '
	git checkout -b testing-1 master^ &&
	git checkout -b testing-2 master~2 &&
	git checkout master
'

test_expect_success '"log -" should work' '
	git log testing-2 >expect &&
	git log - >actual &&
	test_cmp expect actual
'

test_expect_success 'symmetric revision range should work when one end is left empty' '
	git checkout testing-2 &&
	git checkout master &&
	git log ...@{-1} >expect.first_empty &&
	git log @{-1}... >expect.last_empty &&
	git log ...- >actual.first_empty &&
	git log -... >actual.last_empty &&
	test_cmp expect.first_empty actual.first_empty &&
	test_cmp expect.last_empty actual.last_empty
'

test_expect_success 'asymmetric revision range should work when one end is left empty' '
	git checkout testing-2 &&
	git checkout master &&
	git log ..@{-1} >expect.first_empty &&
	git log @{-1}.. >expect.last_empty &&
	git log ..- >actual.first_empty &&
	git log -.. >actual.last_empty &&
	test_cmp expect.first_empty actual.first_empty &&
	test_cmp expect.last_empty actual.last_empty
'

test_expect_success 'symmetric revision range should work when both ends are given' '
	git checkout testing-2 &&
	git checkout master &&
	git log -...testing-1 >expect &&
	git log testing-2...testing-1 >actual &&
	test_cmp expect actual
'

test_expect_success 'asymmetric revision range should work when both ends are given' '
	git checkout testing-2 &&
	git checkout master &&
	git log -..testing-1 >expect &&
	git log testing-2..testing-1 >actual &&
	test_cmp expect actual
'

test_expect_success 'multiple separate arguments should be handled properly' '
	git checkout testing-2 &&
	git checkout master &&
	git log - - >expect.1 &&
	git log @{-1} @{-1} >actual.1 &&
	git log - HEAD >expect.2 &&
	git log @{-1} HEAD >actual.2 &&
	test_cmp expect.1 actual.1 &&
	test_cmp expect.2 actual.2
'

test_expect_success 'revision ranges with same start and end should be empty' '
	git checkout testing-2 &&
	git checkout master &&
	test 0 -eq $(git log -...- | wc -l) &&
	test 0 -eq $(git log -..- | wc -l)
'

test_expect_success 'suffixes to - should work' '
	git checkout testing-2 &&
	git checkout master &&
	git log -~ >expect.1 &&
	git log @{-1}~ >actual.1 &&
	git log -~2 >expect.2 &&
	git log @{-1}~2 >actual.2 &&
	git log -^ >expect.3 &&
	git log @{-1}^ >actual.3 &&
	# git log -@{yesterday} >expect.4 &&
	# git log @{-1}@{yesterday} >actual.4 &&
	test_cmp expect.1 actual.1 &&
	test_cmp expect.2 actual.2 &&
	test_cmp expect.3 actual.3
	# test_cmp expect.4 actual.4
'

test_done
