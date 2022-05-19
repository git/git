#!/bin/sh

test_description='clone --branch option'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

check_HEAD() {
	echo refs/heads/"$1" >expect &&
	but symbolic-ref HEAD >actual &&
	test_cmp expect actual
}

check_file() {
	echo "$1" >expect &&
	test_cmp expect file
}

test_expect_success 'setup' '
	mkdir parent &&
	(cd parent && but init &&
	 echo one >file && but add file && but cummit -m one &&
	 but checkout -b two &&
	 echo two >file && but add file && but cummit -m two &&
	 but checkout main) &&
	mkdir empty &&
	(cd empty && but init)
'

test_expect_success 'vanilla clone chooses HEAD' '
	but clone parent clone &&
	(cd clone &&
	 check_HEAD main &&
	 check_file one
	)
'

test_expect_success 'clone -b chooses specified branch' '
	but clone -b two parent clone-two &&
	(cd clone-two &&
	 check_HEAD two &&
	 check_file two
	)
'

test_expect_success 'clone -b sets up tracking' '
	(cd clone-two &&
	 echo origin >expect &&
	 but config branch.two.remote >actual &&
	 echo refs/heads/two >>expect &&
	 but config branch.two.merge >>actual &&
	 test_cmp expect actual
	)
'

test_expect_success 'clone -b does not munge remotes/origin/HEAD' '
	(cd clone-two &&
	 echo refs/remotes/origin/main >expect &&
	 but symbolic-ref refs/remotes/origin/HEAD >actual &&
	 test_cmp expect actual
	)
'

test_expect_success 'clone -b with bogus branch' '
	test_must_fail but clone -b bogus parent clone-bogus
'

test_expect_success 'clone -b not allowed with empty repos' '
	test_must_fail but clone -b branch empty clone-branch-empty
'

test_done
