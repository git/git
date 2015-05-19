#!/bin/sh

test_description='"git merge" top-level frontend'

. ./test-lib.sh

t3033_reset () {
	git checkout -B master two &&
	git branch -f left three &&
	git branch -f right four
}

test_expect_success setup '
	test_commit one &&
	git branch left &&
	git branch right &&
	test_commit two &&
	git checkout left &&
	test_commit three &&
	git checkout right &&
	test_commit four &&
	git checkout master
'

# Local branches

test_expect_success 'merge an octopus into void' '
	t3033_reset &&
	git checkout --orphan test &&
	git rm -fr . &&
	test_must_fail git merge left right &&
	test_must_fail git rev-parse --verify HEAD &&
	git diff --quiet &&
	test_must_fail git rev-parse HEAD
'

test_expect_success 'merge an octopus, fast-forward (ff)' '
	t3033_reset &&
	git reset --hard one &&
	git merge left right &&
	# one is ancestor of three (left) and four (right)
	test_must_fail git rev-parse --verify HEAD^3 &&
	git rev-parse HEAD^1 HEAD^2 | sort >actual &&
	git rev-parse three four | sort >expect &&
	test_cmp expect actual
'

test_expect_success 'merge octopus, non-fast-forward (ff)' '
	t3033_reset &&
	git reset --hard one &&
	git merge --no-ff left right &&
	# one is ancestor of three (left) and four (right)
	test_must_fail git rev-parse --verify HEAD^4 &&
	git rev-parse HEAD^1 HEAD^2 HEAD^3 | sort >actual &&
	git rev-parse one three four | sort >expect &&
	test_cmp expect actual
'

test_expect_success 'merge octopus, fast-forward (does not ff)' '
	t3033_reset &&
	git merge left right &&
	# two (master) is not an ancestor of three (left) and four (right)
	test_must_fail git rev-parse --verify HEAD^4 &&
	git rev-parse HEAD^1 HEAD^2 HEAD^3 | sort >actual &&
	git rev-parse two three four | sort >expect &&
	test_cmp expect actual
'

test_expect_success 'merge octopus, non-fast-forward' '
	t3033_reset &&
	git merge --no-ff left right &&
	test_must_fail git rev-parse --verify HEAD^4 &&
	git rev-parse HEAD^1 HEAD^2 HEAD^3 | sort >actual &&
	git rev-parse two three four | sort >expect &&
	test_cmp expect actual
'

# The same set with FETCH_HEAD

test_expect_success 'merge FETCH_HEAD octopus into void' '
	t3033_reset &&
	git checkout --orphan test &&
	git rm -fr . &&
	git fetch . left right &&
	test_must_fail git merge FETCH_HEAD &&
	test_must_fail git rev-parse --verify HEAD &&
	git diff --quiet &&
	test_must_fail git rev-parse HEAD
'

test_expect_success 'merge FETCH_HEAD octopus fast-forward (ff)' '
	t3033_reset &&
	git reset --hard one &&
	git fetch . left right &&
	git merge FETCH_HEAD &&
	# one is ancestor of three (left) and four (right)
	test_must_fail git rev-parse --verify HEAD^3 &&
	git rev-parse HEAD^1 HEAD^2 | sort >actual &&
	git rev-parse three four | sort >expect &&
	test_cmp expect actual
'

test_expect_success 'merge FETCH_HEAD octopus non-fast-forward (ff)' '
	t3033_reset &&
	git reset --hard one &&
	git fetch . left right &&
	git merge --no-ff FETCH_HEAD &&
	# one is ancestor of three (left) and four (right)
	test_must_fail git rev-parse --verify HEAD^4 &&
	git rev-parse HEAD^1 HEAD^2 HEAD^3 | sort >actual &&
	git rev-parse one three four | sort >expect &&
	test_cmp expect actual
'

test_expect_success 'merge FETCH_HEAD octopus fast-forward (does not ff)' '
	t3033_reset &&
	git fetch . left right &&
	git merge FETCH_HEAD &&
	# two (master) is not an ancestor of three (left) and four (right)
	test_must_fail git rev-parse --verify HEAD^4 &&
	git rev-parse HEAD^1 HEAD^2 HEAD^3 | sort >actual &&
	git rev-parse two three four | sort >expect &&
	test_cmp expect actual
'

test_expect_success 'merge FETCH_HEAD octopus non-fast-forward' '
	t3033_reset &&
	git fetch . left right &&
	git merge --no-ff FETCH_HEAD &&
	test_must_fail git rev-parse --verify HEAD^4 &&
	git rev-parse HEAD^1 HEAD^2 HEAD^3 | sort >actual &&
	git rev-parse two three four | sort >expect &&
	test_cmp expect actual
'

test_done
