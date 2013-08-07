#!/bin/sh

test_description='ext::cmd remote "connect" helper'
. ./test-lib.sh

test_expect_success setup '
	test_tick &&
	git commit --allow-empty -m initial &&
	test_tick &&
	git commit --allow-empty -m second &&
	test_tick &&
	git commit --allow-empty -m third &&
	test_tick &&
	git tag -a -m "tip three" three &&

	test_tick &&
	git commit --allow-empty -m fourth
'

test_expect_success clone '
	cmd=$(echo "echo >&2 ext::sh invoked && %S .." | sed -e "s/ /% /g") &&
	git clone "ext::sh -c %S% ." dst &&
	git for-each-ref refs/heads/ refs/tags/ >expect &&
	(
		cd dst &&
		git config remote.origin.url "ext::sh -c $cmd" &&
		git for-each-ref refs/heads/ refs/tags/
	) >actual &&
	test_cmp expect actual
'

test_expect_success 'update following tag' '
	test_tick &&
	git commit --allow-empty -m fifth &&
	test_tick &&
	git tag -a -m "tip five" five &&
	git for-each-ref refs/heads/ refs/tags/ >expect &&
	(
		cd dst &&
		git pull &&
		git for-each-ref refs/heads/ refs/tags/ >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'update backfilled tag' '
	test_tick &&
	git commit --allow-empty -m sixth &&
	test_tick &&
	git tag -a -m "tip two" two three^1 &&
	git for-each-ref refs/heads/ refs/tags/ >expect &&
	(
		cd dst &&
		git pull &&
		git for-each-ref refs/heads/ refs/tags/ >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'update backfilled tag without primary transfer' '
	test_tick &&
	git tag -a -m "tip one " one two^1 &&
	git for-each-ref refs/heads/ refs/tags/ >expect &&
	(
		cd dst &&
		git pull &&
		git for-each-ref refs/heads/ refs/tags/ >../actual
	) &&
	test_cmp expect actual
'

test_done
