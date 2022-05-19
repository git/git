#!/bin/sh

test_description='tests for ref^{stuff}'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	echo blob >a-blob &&
	but tag -a -m blob blob-tag $(but hash-object -w a-blob) &&
	mkdir a-tree &&
	echo moreblobs >a-tree/another-blob &&
	but add . &&
	TREE_SHA1=$(but write-tree) &&
	but tag -a -m tree tree-tag "$TREE_SHA1" &&
	but cummit -m Initial &&
	but tag -a -m cummit cummit-tag &&
	but branch ref &&
	but checkout main &&
	echo modified >>a-blob &&
	but add -u &&
	but cummit -m Modified &&
	but branch modref &&
	echo changed! >>a-blob &&
	but add -u &&
	but cummit -m !Exp &&
	but branch expref &&
	echo changed >>a-blob &&
	but add -u &&
	but cummit -m Changed &&
	echo changed-again >>a-blob &&
	but add -u &&
	but cummit -m Changed-again
'

test_expect_success 'ref^{non-existent}' '
	test_must_fail but rev-parse ref^{non-existent}
'

test_expect_success 'ref^{}' '
	but rev-parse ref >expected &&
	but rev-parse ref^{} >actual &&
	test_cmp expected actual &&
	but rev-parse cummit-tag^{} >actual &&
	test_cmp expected actual
'

test_expect_success 'ref^{cummit}' '
	but rev-parse ref >expected &&
	but rev-parse ref^{cummit} >actual &&
	test_cmp expected actual &&
	but rev-parse cummit-tag^{cummit} >actual &&
	test_cmp expected actual &&
	test_must_fail but rev-parse tree-tag^{cummit} &&
	test_must_fail but rev-parse blob-tag^{cummit}
'

test_expect_success 'ref^{tree}' '
	echo $TREE_SHA1 >expected &&
	but rev-parse ref^{tree} >actual &&
	test_cmp expected actual &&
	but rev-parse cummit-tag^{tree} >actual &&
	test_cmp expected actual &&
	but rev-parse tree-tag^{tree} >actual &&
	test_cmp expected actual &&
	test_must_fail but rev-parse blob-tag^{tree}
'

test_expect_success 'ref^{tag}' '
	test_must_fail but rev-parse HEAD^{tag} &&
	but rev-parse cummit-tag >expected &&
	but rev-parse cummit-tag^{tag} >actual &&
	test_cmp expected actual
'

test_expect_success 'ref^{/.}' '
	but rev-parse main >expected &&
	but rev-parse main^{/.} >actual &&
	test_cmp expected actual
'

test_expect_success 'ref^{/non-existent}' '
	test_must_fail but rev-parse main^{/non-existent}
'

test_expect_success 'ref^{/Initial}' '
	but rev-parse ref >expected &&
	but rev-parse main^{/Initial} >actual &&
	test_cmp expected actual
'

test_expect_success 'ref^{/!Exp}' '
	test_must_fail but rev-parse main^{/!Exp}
'

test_expect_success 'ref^{/!}' '
	test_must_fail but rev-parse main^{/!}
'

test_expect_success 'ref^{/!!Exp}' '
	but rev-parse expref >expected &&
	but rev-parse main^{/!!Exp} >actual &&
	test_cmp expected actual
'

test_expect_success 'ref^{/!-}' '
	test_must_fail but rev-parse main^{/!-}
'

test_expect_success 'ref^{/!-.}' '
	test_must_fail but rev-parse main^{/!-.}
'

test_expect_success 'ref^{/!-non-existent}' '
	but rev-parse main >expected &&
	but rev-parse main^{/!-non-existent} >actual &&
	test_cmp expected actual
'

test_expect_success 'ref^{/!-Changed}' '
	but rev-parse expref >expected &&
	but rev-parse main^{/!-Changed} >actual &&
	test_cmp expected actual
'

test_expect_success 'ref^{/!-!Exp}' '
	but rev-parse modref >expected &&
	but rev-parse expref^{/!-!Exp} >actual &&
	test_cmp expected actual
'

test_done
