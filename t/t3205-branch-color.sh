#!/bin/sh

test_description='basic branch output coloring'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'set up some sample branches' '
	test_commit foo &&
	git branch -M main &&
	git update-ref refs/remotes/origin/main HEAD &&
	git update-ref refs/heads/other HEAD
'

# choose non-default colors to make sure config
# is taking effect
test_expect_success 'set up some color config' '
	git config color.branch.local blue &&
	git config color.branch.remote yellow &&
	git config color.branch.current cyan
'

test_expect_success 'regular output shows colors' '
	cat >expect <<-\EOF &&
	* <CYAN>main<RESET>
	  <BLUE>other<RESET>
	  <YELLOW>remotes/origin/main<RESET>
	EOF
	git branch --color -a >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success 'verbose output shows colors' '
	oid=$(git rev-parse --short HEAD) &&
	cat >expect <<-EOF &&
	* <CYAN>main               <RESET> $oid foo
	  <BLUE>other              <RESET> $oid foo
	  <YELLOW>remotes/origin/main<RESET> $oid foo
	EOF
	git branch --color -v -a >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect actual
'

test_done
