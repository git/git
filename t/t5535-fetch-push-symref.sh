#!/bin/sh

test_description='avoiding conflicting update through symref aliasing'

. ./test-lib.sh

test_expect_success 'setup' '
	test_cummit one &&
	but clone . src &&
	but clone src dst1 &&
	but clone src dst2 &&
	test_cummit two &&
	( cd src && but pull )
'

test_expect_success 'push' '
	(
		cd src &&
		but push ../dst1 "refs/remotes/*:refs/remotes/*"
	) &&
	but ls-remote src "refs/remotes/*" >expect &&
	but ls-remote dst1 "refs/remotes/*" >actual &&
	test_cmp expect actual &&
	( cd src && but symbolic-ref refs/remotes/origin/HEAD ) >expect &&
	( cd dst1 && but symbolic-ref refs/remotes/origin/HEAD ) >actual &&
	test_cmp expect actual
'

test_expect_success 'fetch' '
	(
		cd dst2 &&
		but fetch ../src "refs/remotes/*:refs/remotes/*"
	) &&
	but ls-remote src "refs/remotes/*" >expect &&
	but ls-remote dst2 "refs/remotes/*" >actual &&
	test_cmp expect actual &&
	( cd src && but symbolic-ref refs/remotes/origin/HEAD ) >expect &&
	( cd dst2 && but symbolic-ref refs/remotes/origin/HEAD ) >actual &&
	test_cmp expect actual
'

test_done
