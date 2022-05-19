#!/bin/sh

test_description='basic sanity checks for but var'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'get BUT_AUTHOR_IDENT' '
	test_tick &&
	echo "$BUT_AUTHOR_NAME <$BUT_AUTHOR_EMAIL> $BUT_AUTHOR_DATE" >expect &&
	but var BUT_AUTHOR_IDENT >actual &&
	test_cmp expect actual
'

test_expect_success 'get BUT_CUMMITTER_IDENT' '
	test_tick &&
	echo "$BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE" >expect &&
	but var BUT_CUMMITTER_IDENT >actual &&
	test_cmp expect actual
'

test_expect_success !FAIL_PREREQS,!AUTOIDENT 'requested identities are strict' '
	(
		sane_unset BUT_CUMMITTER_NAME &&
		sane_unset BUT_CUMMITTER_EMAIL &&
		test_must_fail but var BUT_CUMMITTER_IDENT
	)
'

test_expect_success 'get BUT_DEFAULT_BRANCH without configuration' '
	(
		sane_unset BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME &&
		but init defbranch &&
		but -C defbranch symbolic-ref --short HEAD >expect &&
		but var BUT_DEFAULT_BRANCH >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get BUT_DEFAULT_BRANCH with configuration' '
	test_config init.defaultbranch foo &&
	(
		sane_unset BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME &&
		echo foo >expect &&
		but var BUT_DEFAULT_BRANCH >actual &&
		test_cmp expect actual
	)
'

# For but var -l, we check only a representative variable;
# testing the whole output would make our test too brittle with
# respect to unrelated changes in the test suite's environment.
test_expect_success 'but var -l lists variables' '
	but var -l >actual &&
	echo "$BUT_AUTHOR_NAME <$BUT_AUTHOR_EMAIL> $BUT_AUTHOR_DATE" >expect &&
	sed -n s/BUT_AUTHOR_IDENT=//p <actual >actual.author &&
	test_cmp expect actual.author
'

test_expect_success 'but var -l lists config' '
	but var -l >actual &&
	echo false >expect &&
	sed -n s/core\\.bare=//p <actual >actual.bare &&
	test_cmp expect actual.bare
'

test_expect_success 'listing and asking for variables are exclusive' '
	test_must_fail but var -l BUT_CUMMITTER_IDENT
'

test_done
