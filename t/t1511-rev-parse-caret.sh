#!/bin/sh

test_description='tests for ref^{stuff}'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	echo blob >a-blob &&
	git tag -a -m blob blob-tag $(git hash-object -w a-blob) &&
	mkdir a-tree &&
	echo moreblobs >a-tree/another-blob &&
	git add . &&
	TREE_SHA1=$(git write-tree) &&
	git tag -a -m tree tree-tag "$TREE_SHA1" &&
	git commit -m Initial &&
	git tag -a -m commit commit-tag &&
	git branch ref &&
	git checkout main &&
	echo modified >>a-blob &&
	git add -u &&
	git commit -m Modified &&
	git branch modref &&
	echo changed! >>a-blob &&
	git add -u &&
	git commit -m !Exp &&
	git branch expref &&
	echo changed >>a-blob &&
	git add -u &&
	git commit -m Changed &&
	echo changed-again >>a-blob &&
	git add -u &&
	git commit -m Changed-again
'

test_expect_success 'ref^{non-existent}' '
	test_must_fail git rev-parse ref^{non-existent}
'

test_expect_success 'ref^{}' '
	git rev-parse ref >expected &&
	git rev-parse ref^{} >actual &&
	test_cmp expected actual &&
	git rev-parse commit-tag^{} >actual &&
	test_cmp expected actual
'

test_expect_success 'ref^{commit}' '
	git rev-parse ref >expected &&
	git rev-parse ref^{commit} >actual &&
	test_cmp expected actual &&
	git rev-parse commit-tag^{commit} >actual &&
	test_cmp expected actual &&
	test_must_fail git rev-parse tree-tag^{commit} &&
	test_must_fail git rev-parse blob-tag^{commit}
'

test_expect_success 'ref^{tree}' '
	echo $TREE_SHA1 >expected &&
	git rev-parse ref^{tree} >actual &&
	test_cmp expected actual &&
	git rev-parse commit-tag^{tree} >actual &&
	test_cmp expected actual &&
	git rev-parse tree-tag^{tree} >actual &&
	test_cmp expected actual &&
	test_must_fail git rev-parse blob-tag^{tree}
'

test_expect_success 'ref^{tag}' '
	test_must_fail git rev-parse HEAD^{tag} &&
	git rev-parse commit-tag >expected &&
	git rev-parse commit-tag^{tag} >actual &&
	test_cmp expected actual
'

test_expect_success 'ref^{/.}' '
	git rev-parse main >expected &&
	git rev-parse main^{/.} >actual &&
	test_cmp expected actual
'

test_expect_success 'ref^{/non-existent}' '
	test_must_fail git rev-parse main^{/non-existent}
'

test_expect_success 'ref^{/Initial}' '
	git rev-parse ref >expected &&
	git rev-parse main^{/Initial} >actual &&
	test_cmp expected actual
'

test_expect_success 'ref^{/!Exp}' '
	test_must_fail git rev-parse main^{/!Exp}
'

test_expect_success 'ref^{/!}' '
	test_must_fail git rev-parse main^{/!}
'

test_expect_success 'ref^{/!!Exp}' '
	git rev-parse expref >expected &&
	git rev-parse main^{/!!Exp} >actual &&
	test_cmp expected actual
'

test_expect_success 'ref^{/!-}' '
	test_must_fail git rev-parse main^{/!-}
'

test_expect_success 'ref^{/!-.}' '
	test_must_fail git rev-parse main^{/!-.}
'

test_expect_success 'ref^{/!-non-existent}' '
	git rev-parse main >expected &&
	git rev-parse main^{/!-non-existent} >actual &&
	test_cmp expected actual
'

test_expect_success 'ref^{/!-Changed}' '
	git rev-parse expref >expected &&
	git rev-parse main^{/!-Changed} >actual &&
	test_cmp expected actual
'

test_expect_success 'ref^{/!-!Exp}' '
	git rev-parse modref >expected &&
	git rev-parse expref^{/!-!Exp} >actual &&
	test_cmp expected actual
'

test_done
