#!/bin/sh

test_description='clone --branch option'
. ./test-lib.sh

check_HEAD() {
	echo refs/heads/"$1" >expect &&
	git symbolic-ref HEAD >actual &&
	test_cmp expect actual
}

check_file() {
	echo "$1" >expect &&
	test_cmp expect file
}

test_expect_success 'setup' '
	mkdir parent &&
	(cd parent && git init &&
	 echo one >file && git add file && git commit -m one &&
	 git checkout -b two &&
	 echo two >file && git add file && git commit -m two &&
	 git checkout master)
'

test_expect_success 'vanilla clone chooses HEAD' '
	git clone parent clone &&
	(cd clone &&
	 check_HEAD master &&
	 check_file one
	)
'

test_expect_success 'clone -b chooses specified branch' '
	git clone -b two parent clone-two &&
	(cd clone-two &&
	 check_HEAD two &&
	 check_file two
	)
'

test_expect_success 'clone -b sets up tracking' '
	(cd clone-two &&
	 echo origin >expect &&
	 git config branch.two.remote >actual &&
	 echo refs/heads/two >>expect &&
	 git config branch.two.merge >>actual &&
	 test_cmp expect actual
	)
'

test_expect_success 'clone -b does not munge remotes/origin/HEAD' '
	(cd clone-two &&
	 echo refs/remotes/origin/master >expect &&
	 git symbolic-ref refs/remotes/origin/HEAD >actual &&
	 test_cmp expect actual
	)
'

test_expect_success 'clone -b with bogus branch chooses HEAD' '
	git clone -b bogus parent clone-bogus &&
	(cd clone-bogus &&
	 check_HEAD master &&
	 check_file one
	)
'

test_done
