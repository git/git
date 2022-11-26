#!/bin/sh

test_description='basic sanity checks for git var'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

sane_unset_all_editors () {
	sane_unset GIT_EDITOR &&
	sane_unset VISUAL &&
	sane_unset EDITOR
}

test_expect_success 'get GIT_AUTHOR_IDENT' '
	test_tick &&
	echo "$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> $GIT_AUTHOR_DATE" >expect &&
	git var GIT_AUTHOR_IDENT >actual &&
	test_cmp expect actual
'

test_expect_success 'get GIT_COMMITTER_IDENT' '
	test_tick &&
	echo "$GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE" >expect &&
	git var GIT_COMMITTER_IDENT >actual &&
	test_cmp expect actual
'

test_expect_success !FAIL_PREREQS,!AUTOIDENT 'requested identities are strict' '
	(
		sane_unset GIT_COMMITTER_NAME &&
		sane_unset GIT_COMMITTER_EMAIL &&
		test_must_fail git var GIT_COMMITTER_IDENT
	)
'

test_expect_success 'get GIT_DEFAULT_BRANCH without configuration' '
	(
		sane_unset GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME &&
		git init defbranch &&
		git -C defbranch symbolic-ref --short HEAD >expect &&
		git var GIT_DEFAULT_BRANCH >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get GIT_DEFAULT_BRANCH with configuration' '
	test_config init.defaultbranch foo &&
	(
		sane_unset GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME &&
		echo foo >expect &&
		git var GIT_DEFAULT_BRANCH >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get GIT_EDITOR without configuration' '
	(
		sane_unset_all_editors &&
		test_expect_code 1 git var GIT_EDITOR >out &&
		test_must_be_empty out
	)
'

test_expect_success 'get GIT_EDITOR with configuration' '
	test_config core.editor foo &&
	(
		sane_unset_all_editors &&
		echo foo >expect &&
		git var GIT_EDITOR >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get GIT_EDITOR with environment variable GIT_EDITOR' '
	(
		sane_unset_all_editors &&
		echo bar >expect &&
		GIT_EDITOR=bar git var GIT_EDITOR >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get GIT_EDITOR with environment variable EDITOR' '
	(
		sane_unset_all_editors &&
		echo bar >expect &&
		EDITOR=bar git var GIT_EDITOR >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get GIT_EDITOR with configuration and environment variable GIT_EDITOR' '
	test_config core.editor foo &&
	(
		sane_unset_all_editors &&
		echo bar >expect &&
		GIT_EDITOR=bar git var GIT_EDITOR >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get GIT_EDITOR with configuration and environment variable EDITOR' '
	test_config core.editor foo &&
	(
		sane_unset_all_editors &&
		echo foo >expect &&
		EDITOR=bar git var GIT_EDITOR >actual &&
		test_cmp expect actual
	)
'

# For git var -l, we check only a representative variable;
# testing the whole output would make our test too brittle with
# respect to unrelated changes in the test suite's environment.
test_expect_success 'git var -l lists variables' '
	git var -l >actual &&
	echo "$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> $GIT_AUTHOR_DATE" >expect &&
	sed -n s/GIT_AUTHOR_IDENT=//p <actual >actual.author &&
	test_cmp expect actual.author
'

test_expect_success 'git var -l lists config' '
	git var -l >actual &&
	echo false >expect &&
	sed -n s/core\\.bare=//p <actual >actual.bare &&
	test_cmp expect actual.bare
'

test_expect_success 'listing and asking for variables are exclusive' '
	test_must_fail git var -l GIT_COMMITTER_IDENT
'

test_done
