#!/bin/sh

test_description='reset --stdin'

. ./test-lib.sh

test_expect_success 'reset --stdin' '
	test_commit hello &&
	git rm hello.t &&
	test -z "$(git ls-files hello.t)" &&
	echo hello.t | git reset --stdin &&
	test hello.t = "$(git ls-files hello.t)"
'

test_expect_success 'reset --stdin -z' '
	test_commit world &&
	git rm hello.t world.t &&
	test -z "$(git ls-files hello.t world.t)" &&
	printf world.tQworld.tQhello.tQ | q_to_nul | git reset --stdin -z &&
	printf "hello.t\nworld.t\n" >expect &&
	git ls-files >actual &&
	test_cmp expect actual
'

test_expect_success '--stdin requires --mixed' '
	echo hello.t >list &&
	test_must_fail git reset --soft --stdin <list &&
	test_must_fail git reset --hard --stdin <list &&
	git reset --mixed --stdin <list
'

test_done
