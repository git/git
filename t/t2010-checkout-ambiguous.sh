#!/bin/sh

test_description='checkout and pathspecs/refspecs ambiguities'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	echo hello >world &&
	echo hello >all &&
	git add all world &&
	git commit -m initial &&
	git branch world
'

test_expect_success 'reference must be a tree' '
	test_must_fail git checkout $(git hash-object ./all) --
'

test_expect_success 'branch switching' '
	test "refs/heads/main" = "$(git symbolic-ref HEAD)" &&
	git checkout world -- &&
	test "refs/heads/world" = "$(git symbolic-ref HEAD)"
'

test_expect_success 'checkout world from the index' '
	echo bye > world &&
	git checkout -- world &&
	git diff --exit-code --quiet
'

test_expect_success 'non ambiguous call' '
	git checkout all
'

test_expect_success 'allow the most common case' '
	git checkout world &&
	test "refs/heads/world" = "$(git symbolic-ref HEAD)"
'

test_expect_success 'check ambiguity' '
	test_must_fail git checkout world all
'

test_expect_success 'check ambiguity in subdir' '
	mkdir sub &&
	# not ambiguous because sub/world does not exist
	git -C sub checkout world ../all &&
	echo hello >sub/world &&
	# ambiguous because sub/world does exist
	test_must_fail git -C sub checkout world ../all
'

test_expect_success 'disambiguate checking out from a tree-ish' '
	echo bye > world &&
	git checkout world -- world &&
	git diff --exit-code --quiet
'

test_expect_success 'accurate error message with more than one ref' '
	test_must_fail git checkout HEAD main -- 2>actual &&
	test_i18ngrep 2 actual &&
	test_i18ngrep "one reference expected, 2 given" actual
'

test_done
