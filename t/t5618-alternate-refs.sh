#!/bin/sh

test_description='test handling of --alternate-refs traversal'
. ./test-lib.sh

# Avoid test_cummit because we want a specific and known set of refs:
#
#  base -- one
#      \      \
#       two -- merged
#
# where "one" and "two" are on separate refs, and "merged" is available only in
# the dependent child repository.
test_expect_success 'set up local refs' '
	but checkout -b one &&
	test_tick &&
	but cummit --allow-empty -m base &&
	test_tick &&
	but cummit --allow-empty -m one &&
	but checkout -b two HEAD^ &&
	test_tick &&
	but cummit --allow-empty -m two
'

# We'll enter the child repository after it's set up since that's where
# all of the subsequent tests will want to run (and it's easy to forget a
# "-C child" and get nonsense results).
test_expect_success 'set up shared clone' '
	but clone -s . child &&
	cd child &&
	but merge origin/one
'

test_expect_success 'rev-list --alternate-refs' '
	but rev-list --remotes=origin >expect &&
	but rev-list --alternate-refs >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-list --not --alternate-refs' '
	but rev-parse HEAD >expect &&
	but rev-list HEAD --not --alternate-refs >actual &&
	test_cmp expect actual
'

test_expect_success 'limiting with alternateRefsPrefixes' '
	test_config core.alternateRefsPrefixes refs/heads/one &&
	but rev-list origin/one >expect &&
	but rev-list --alternate-refs >actual &&
	test_cmp expect actual
'

test_expect_success 'log --source shows .alternate marker' '
	but log --oneline --source --remotes=origin >expect.orig &&
	sed "s/origin.* /.alternate /" <expect.orig >expect &&
	but log --oneline --source --alternate-refs >actual &&
	test_cmp expect actual
'

test_done
