#!/bin/sh

test_description='avoiding conflicting update through symref aliasing'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit one &&
	git clone . src &&
	git clone src dst1 &&
	git clone src dst2 &&
	test_commit two &&
	( cd src && git pull )
'

test_expect_success 'push' '
	(
		cd src &&
		git push ../dst1 "refs/remotes/*:refs/remotes/*"
	) &&
	git ls-remote src "refs/remotes/*" >expect &&
	git ls-remote dst1 "refs/remotes/*" >actual &&
	test_cmp expect actual &&
	( cd src && git symbolic-ref refs/remotes/origin/HEAD ) >expect &&
	( cd dst1 && git symbolic-ref refs/remotes/origin/HEAD ) >actual &&
	test_cmp expect actual
'

test_expect_success 'fetch' '
	(
		cd dst2 &&
		git fetch ../src "refs/remotes/*:refs/remotes/*"
	) &&
	git ls-remote src "refs/remotes/*" >expect &&
	git ls-remote dst2 "refs/remotes/*" >actual &&
	test_cmp expect actual &&
	( cd src && git symbolic-ref refs/remotes/origin/HEAD ) >expect &&
	( cd dst2 && git symbolic-ref refs/remotes/origin/HEAD ) >actual &&
	test_cmp expect actual
'

test_done
