#!/bin/sh

test_description='checkout and pathspecs/refspecs ambiguities'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	echo hello >world &&
	echo hello >all &&
	but add all world &&
	but cummit -m initial &&
	but branch world
'

test_expect_success 'reference must be a tree' '
	test_must_fail but checkout $(but hash-object ./all) --
'

test_expect_success 'branch switching' '
	test "refs/heads/main" = "$(but symbolic-ref HEAD)" &&
	but checkout world -- &&
	test "refs/heads/world" = "$(but symbolic-ref HEAD)"
'

test_expect_success 'checkout world from the index' '
	echo bye > world &&
	but checkout -- world &&
	but diff --exit-code --quiet
'

test_expect_success 'non ambiguous call' '
	but checkout all
'

test_expect_success 'allow the most common case' '
	but checkout world &&
	test "refs/heads/world" = "$(but symbolic-ref HEAD)"
'

test_expect_success 'check ambiguity' '
	test_must_fail but checkout world all
'

test_expect_success 'check ambiguity in subdir' '
	mkdir sub &&
	# not ambiguous because sub/world does not exist
	but -C sub checkout world ../all &&
	echo hello >sub/world &&
	# ambiguous because sub/world does exist
	test_must_fail but -C sub checkout world ../all
'

test_expect_success 'disambiguate checking out from a tree-ish' '
	echo bye > world &&
	but checkout world -- world &&
	but diff --exit-code --quiet
'

test_expect_success 'accurate error message with more than one ref' '
	test_must_fail but checkout HEAD main -- 2>actual &&
	test_i18ngrep 2 actual &&
	test_i18ngrep "one reference expected, 2 given" actual
'

test_done
