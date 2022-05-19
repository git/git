#!/bin/sh

test_description='"but merge" top-level frontend'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

t3033_reset () {
	but checkout -B main two &&
	but branch -f left three &&
	but branch -f right four
}

test_expect_success setup '
	test_cummit one &&
	but branch left &&
	but branch right &&
	test_cummit two &&
	but checkout left &&
	test_cummit three &&
	but checkout right &&
	test_cummit four &&
	but checkout --orphan newroot &&
	test_cummit five &&
	but checkout main
'

# Local branches

test_expect_success 'merge an octopus into void' '
	t3033_reset &&
	but checkout --orphan test &&
	but rm -fr . &&
	test_must_fail but merge left right &&
	test_must_fail but rev-parse --verify HEAD &&
	but diff --quiet &&
	test_must_fail but rev-parse HEAD
'

test_expect_success 'merge an octopus, fast-forward (ff)' '
	t3033_reset &&
	but reset --hard one &&
	but merge left right &&
	# one is ancestor of three (left) and four (right)
	test_must_fail but rev-parse --verify HEAD^3 &&
	but rev-parse HEAD^1 HEAD^2 | sort >actual &&
	but rev-parse three four | sort >expect &&
	test_cmp expect actual
'

test_expect_success 'merge octopus, non-fast-forward (ff)' '
	t3033_reset &&
	but reset --hard one &&
	but merge --no-ff left right &&
	# one is ancestor of three (left) and four (right)
	test_must_fail but rev-parse --verify HEAD^4 &&
	but rev-parse HEAD^1 HEAD^2 HEAD^3 | sort >actual &&
	but rev-parse one three four | sort >expect &&
	test_cmp expect actual
'

test_expect_success 'merge octopus, fast-forward (does not ff)' '
	t3033_reset &&
	but merge left right &&
	# two (main) is not an ancestor of three (left) and four (right)
	test_must_fail but rev-parse --verify HEAD^4 &&
	but rev-parse HEAD^1 HEAD^2 HEAD^3 | sort >actual &&
	but rev-parse two three four | sort >expect &&
	test_cmp expect actual
'

test_expect_success 'merge octopus, non-fast-forward' '
	t3033_reset &&
	but merge --no-ff left right &&
	test_must_fail but rev-parse --verify HEAD^4 &&
	but rev-parse HEAD^1 HEAD^2 HEAD^3 | sort >actual &&
	but rev-parse two three four | sort >expect &&
	test_cmp expect actual
'

# The same set with FETCH_HEAD

test_expect_success 'merge FETCH_HEAD octopus into void' '
	t3033_reset &&
	but checkout --orphan test &&
	but rm -fr . &&
	but fetch . left right &&
	test_must_fail but merge FETCH_HEAD &&
	test_must_fail but rev-parse --verify HEAD &&
	but diff --quiet &&
	test_must_fail but rev-parse HEAD
'

test_expect_success 'merge FETCH_HEAD octopus fast-forward (ff)' '
	t3033_reset &&
	but reset --hard one &&
	but fetch . left right &&
	but merge FETCH_HEAD &&
	# one is ancestor of three (left) and four (right)
	test_must_fail but rev-parse --verify HEAD^3 &&
	but rev-parse HEAD^1 HEAD^2 | sort >actual &&
	but rev-parse three four | sort >expect &&
	test_cmp expect actual
'

test_expect_success 'merge FETCH_HEAD octopus non-fast-forward (ff)' '
	t3033_reset &&
	but reset --hard one &&
	but fetch . left right &&
	but merge --no-ff FETCH_HEAD &&
	# one is ancestor of three (left) and four (right)
	test_must_fail but rev-parse --verify HEAD^4 &&
	but rev-parse HEAD^1 HEAD^2 HEAD^3 | sort >actual &&
	but rev-parse one three four | sort >expect &&
	test_cmp expect actual
'

test_expect_success 'merge FETCH_HEAD octopus fast-forward (does not ff)' '
	t3033_reset &&
	but fetch . left right &&
	but merge FETCH_HEAD &&
	# two (main) is not an ancestor of three (left) and four (right)
	test_must_fail but rev-parse --verify HEAD^4 &&
	but rev-parse HEAD^1 HEAD^2 HEAD^3 | sort >actual &&
	but rev-parse two three four | sort >expect &&
	test_cmp expect actual
'

test_expect_success 'merge FETCH_HEAD octopus non-fast-forward' '
	t3033_reset &&
	but fetch . left right &&
	but merge --no-ff FETCH_HEAD &&
	test_must_fail but rev-parse --verify HEAD^4 &&
	but rev-parse HEAD^1 HEAD^2 HEAD^3 | sort >actual &&
	but rev-parse two three four | sort >expect &&
	test_cmp expect actual
'

# two-project merge
test_expect_success 'refuse two-project merge by default' '
	t3033_reset &&
	but reset --hard four &&
	test_must_fail but merge five
'

test_expect_success 'refuse two-project merge by default, quit before --autostash happens' '
	t3033_reset &&
	but reset --hard four &&
	echo change >>one.t &&
	but diff >expect &&
	test_must_fail but merge --autostash five 2>err &&
	test_i18ngrep ! "stash" err &&
	but diff >actual &&
	test_cmp expect actual
'

test_expect_success 'two-project merge with --allow-unrelated-histories' '
	t3033_reset &&
	but reset --hard four &&
	but merge --allow-unrelated-histories five &&
	but diff --exit-code five
'

test_expect_success 'two-project merge with --allow-unrelated-histories with --autostash' '
	t3033_reset &&
	but reset --hard four &&
	echo change >>one.t &&
	but diff one.t >expect &&
	but merge --allow-unrelated-histories --autostash five 2>err &&
	test_i18ngrep "Applied autostash." err &&
	but diff one.t >actual &&
	test_cmp expect actual
'

test_done
