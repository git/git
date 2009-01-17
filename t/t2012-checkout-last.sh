#!/bin/sh

test_description='checkout can switch to last branch'

. ./test-lib.sh

test_expect_success 'setup' '
	echo hello >world &&
	git add world &&
	git commit -m initial &&
	git branch other &&
	echo "hello again" >>world &&
	git add world &&
	git commit -m second
'

test_expect_success '"checkout -" does not work initially' '
	test_must_fail git checkout -
'

test_expect_success 'first branch switch' '
	git checkout other
'

test_expect_success '"checkout -" switches back' '
	git checkout - &&
	test "z$(git symbolic-ref HEAD)" = "zrefs/heads/master"
'

test_expect_success '"checkout -" switches forth' '
	git checkout - &&
	test "z$(git symbolic-ref HEAD)" = "zrefs/heads/other"
'

test_expect_success 'detach HEAD' '
	git checkout $(git rev-parse HEAD)
'

test_expect_success '"checkout -" attaches again' '
	git checkout - &&
	test "z$(git symbolic-ref HEAD)" = "zrefs/heads/other"
'

test_expect_success '"checkout -" detaches again' '
	git checkout - &&
	test "z$(git rev-parse HEAD)" = "z$(git rev-parse other)" &&
	test_must_fail git symbolic-ref HEAD
'

test_done
