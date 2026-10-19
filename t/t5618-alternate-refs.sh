#!/bin/sh

test_description='test handling of --alternate-refs traversal'

. ./test-lib.sh

# Avoid test_commit because we want a specific and known set of refs:
#
#  base -- one
#      \      \
#       two -- merged
#
# where "one" and "two" are on separate refs, and "merged" is available only in
# the dependent child repository.
test_expect_success 'set up local refs' '
	git checkout -b one &&
	test_tick &&
	git commit --allow-empty -m base &&
	test_tick &&
	git commit --allow-empty -m one &&
	git checkout -b two HEAD^ &&
	test_tick &&
	git commit --allow-empty -m two
'

# We'll enter the child repository after it's set up since that's where
# all of the subsequent tests will want to run (and it's easy to forget a
# "-C child" and get nonsense results).
test_expect_success 'set up shared clone' '
	git clone -s . child &&
	cd child &&
	git merge origin/one
'

test_expect_success 'rev-list --alternate-refs' '
	git rev-list --remotes=origin >expect &&
	git rev-list --alternate-refs >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-list --not --alternate-refs' '
	git rev-parse HEAD >expect &&
	git rev-list HEAD --not --alternate-refs >actual &&
	test_cmp expect actual
'

test_expect_success 'limiting with alternateRefsPrefixes' '
	test_config core.alternateRefsPrefixes refs/heads/one &&
	git rev-list origin/one >expect &&
	git rev-list --alternate-refs >actual &&
	test_cmp expect actual
'

test_expect_success 'log --source shows .alternate marker' '
	git log --oneline --source --remotes=origin >expect.orig &&
	sed "s/origin.* /.alternate /" <expect.orig >expect &&
	git log --oneline --source --alternate-refs >actual &&
	test_cmp expect actual
'

test_expect_success 'connectivity check of a fetch excludes alternate refs' '
	GIT_TRACE2_EVENT="$(pwd)/fetch.trace" git fetch origin &&
	test_subcommand_flex git rev-list --objects --stdin --not --all \
		--alternate-refs --quiet <fetch.trace
'

test_expect_success 'connectivity check of a deepening fetch excludes alternate refs' '
	GIT_TRACE2_EVENT="$(pwd)/deepen.trace" git fetch --depth=1 origin one &&
	test_subcommand_flex git rev-list --objects --stdin --not \
		--alternate-refs --quiet <deepen.trace
'

test_done
