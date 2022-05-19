#!/bin/sh

test_description='rev-list/rev-parse --glob'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

cummit () {
	test_tick &&
	echo $1 > foo &&
	but add foo &&
	but cummit -m "$1"
}

compare () {
	# Split arguments on whitespace.
	but $1 $2 >expected &&
	but $1 $3 >actual &&
	test_cmp expected actual
}

test_expect_success 'setup' '

	cummit main &&
	but checkout -b subspace/one main &&
	cummit one &&
	but checkout -b subspace/two main &&
	cummit two &&
	but checkout -b subspace-x main &&
	cummit subspace-x &&
	but checkout -b other/three main &&
	cummit three &&
	but checkout -b someref main &&
	cummit some &&
	but checkout main &&
	cummit topic_2 &&
	but tag foo/bar main &&
	cummit topic_3 &&
	but update-ref refs/remotes/foo/baz main &&
	cummit topic_4 &&
	but update-ref refs/remotes/upstream/one subspace/one &&
	but update-ref refs/remotes/upstream/two subspace/two &&
	but update-ref refs/remotes/upstream/x subspace-x &&
	but tag qux/one subspace/one &&
	but tag qux/two subspace/two &&
	but tag qux/x subspace-x
'

test_expect_success 'rev-parse --glob=refs/heads/subspace/*' '

	compare rev-parse "subspace/one subspace/two" "--glob=refs/heads/subspace/*"

'

test_expect_success 'rev-parse --glob=heads/subspace/*' '

	compare rev-parse "subspace/one subspace/two" "--glob=heads/subspace/*"

'

test_expect_success 'rev-parse --glob=refs/heads/subspace/' '

	compare rev-parse "subspace/one subspace/two" "--glob=refs/heads/subspace/"

'

test_expect_success 'rev-parse --glob=heads/subspace/' '

	compare rev-parse "subspace/one subspace/two" "--glob=heads/subspace/"

'

test_expect_success 'rev-parse --glob=heads/subspace' '

	compare rev-parse "subspace/one subspace/two" "--glob=heads/subspace"

'

test_expect_failure 'rev-parse accepts --glob as detached option' '

	compare rev-parse "subspace/one subspace/two" "--glob heads/subspace"

'

test_expect_failure 'rev-parse is not confused by option-like glob' '

	compare rev-parse "main" "--glob --symbolic main"

'

test_expect_success 'rev-parse --branches=subspace/*' '

	compare rev-parse "subspace/one subspace/two" "--branches=subspace/*"

'

test_expect_success 'rev-parse --branches=subspace/' '

	compare rev-parse "subspace/one subspace/two" "--branches=subspace/"

'

test_expect_success 'rev-parse --branches=subspace' '

	compare rev-parse "subspace/one subspace/two" "--branches=subspace"

'

test_expect_success 'rev-parse --glob=heads/subspace/* --glob=heads/other/*' '

	compare rev-parse "subspace/one subspace/two other/three" "--glob=heads/subspace/* --glob=heads/other/*"

'

test_expect_success 'rev-parse --glob=heads/someref/* main' '

	compare rev-parse "main" "--glob=heads/someref/* main"

'

test_expect_success 'rev-parse --glob=heads/*' '

	compare rev-parse "main other/three someref subspace-x subspace/one subspace/two" "--glob=heads/*"

'

test_expect_success 'rev-parse --tags=foo' '

	compare rev-parse "foo/bar" "--tags=foo"

'

test_expect_success 'rev-parse --remotes=foo' '

	compare rev-parse "foo/baz" "--remotes=foo"

'

test_expect_success 'rev-parse --exclude with --branches' '
	compare rev-parse "--exclude=*/* --branches" "main someref subspace-x"
'

test_expect_success 'rev-parse --exclude with --all' '
	compare rev-parse "--exclude=refs/remotes/* --all" "--branches --tags"
'

test_expect_success 'rev-parse accumulates multiple --exclude' '
	compare rev-parse "--exclude=refs/remotes/* --exclude=refs/tags/* --all" --branches
'

test_expect_success 'rev-parse --branches clears --exclude' '
	compare rev-parse "--exclude=* --branches --branches" "--branches"
'

test_expect_success 'rev-parse --tags clears --exclude' '
	compare rev-parse "--exclude=* --tags --tags" "--tags"
'

test_expect_success 'rev-parse --all clears --exclude' '
	compare rev-parse "--exclude=* --all --all" "--all"
'

test_expect_success 'rev-parse --exclude=glob with --branches=glob' '
	compare rev-parse "--exclude=subspace-* --branches=sub*" "subspace/one subspace/two"
'

test_expect_success 'rev-parse --exclude=glob with --tags=glob' '
	compare rev-parse "--exclude=qux/? --tags=qux/*" "qux/one qux/two"
'

test_expect_success 'rev-parse --exclude=glob with --remotes=glob' '
	compare rev-parse "--exclude=upstream/? --remotes=upstream/*" "upstream/one upstream/two"
'

test_expect_success 'rev-parse --exclude=ref with --branches=glob' '
	compare rev-parse "--exclude=subspace-x --branches=sub*" "subspace/one subspace/two"
'

test_expect_success 'rev-parse --exclude=ref with --tags=glob' '
	compare rev-parse "--exclude=qux/x --tags=qux/*" "qux/one qux/two"
'

test_expect_success 'rev-parse --exclude=ref with --remotes=glob' '
	compare rev-parse "--exclude=upstream/x --remotes=upstream/*" "upstream/one upstream/two"
'

test_expect_success 'rev-list --exclude=glob with --branches=glob' '
	compare rev-list "--exclude=subspace-* --branches=sub*" "subspace/one subspace/two"
'

test_expect_success 'rev-list --exclude=glob with --tags=glob' '
	compare rev-list "--exclude=qux/? --tags=qux/*" "qux/one qux/two"
'

test_expect_success 'rev-list --exclude=glob with --remotes=glob' '
	compare rev-list "--exclude=upstream/? --remotes=upstream/*" "upstream/one upstream/two"
'

test_expect_success 'rev-list --exclude=ref with --branches=glob' '
	compare rev-list "--exclude=subspace-x --branches=sub*" "subspace/one subspace/two"
'

test_expect_success 'rev-list --exclude=ref with --tags=glob' '
	compare rev-list "--exclude=qux/x --tags=qux/*" "qux/one qux/two"
'

test_expect_success 'rev-list --exclude=ref with --remotes=glob' '
	compare rev-list "--exclude=upstream/x --remotes=upstream/*" "upstream/one upstream/two"
'

test_expect_success 'rev-list --glob=refs/heads/subspace/*' '

	compare rev-list "subspace/one subspace/two" "--glob=refs/heads/subspace/*"

'

test_expect_success 'rev-list --glob refs/heads/subspace/*' '

	compare rev-list "subspace/one subspace/two" "--glob refs/heads/subspace/*"

'

test_expect_success 'rev-list not confused by option-like --glob arg' '

	compare rev-list "main" "--glob -0 main"

'

test_expect_success 'rev-list --glob=heads/subspace/*' '

	compare rev-list "subspace/one subspace/two" "--glob=heads/subspace/*"

'

test_expect_success 'rev-list --glob=refs/heads/subspace/' '

	compare rev-list "subspace/one subspace/two" "--glob=refs/heads/subspace/"

'

test_expect_success 'rev-list --glob=heads/subspace/' '

	compare rev-list "subspace/one subspace/two" "--glob=heads/subspace/"

'

test_expect_success 'rev-list --glob=heads/subspace' '

	compare rev-list "subspace/one subspace/two" "--glob=heads/subspace"

'

test_expect_success 'rev-list --branches=subspace/*' '

	compare rev-list "subspace/one subspace/two" "--branches=subspace/*"

'

test_expect_success 'rev-list --branches=subspace/' '

	compare rev-list "subspace/one subspace/two" "--branches=subspace/"

'

test_expect_success 'rev-list --branches=subspace' '

	compare rev-list "subspace/one subspace/two" "--branches=subspace"

'

test_expect_success 'rev-list --branches' '

	compare rev-list "main subspace-x someref other/three subspace/one subspace/two" "--branches"

'

test_expect_success 'rev-list --glob=heads/someref/* main' '

	compare rev-list "main" "--glob=heads/someref/* main"

'

test_expect_success 'rev-list --glob=heads/subspace/* --glob=heads/other/*' '

	compare rev-list "subspace/one subspace/two other/three" "--glob=heads/subspace/* --glob=heads/other/*"

'

test_expect_success 'rev-list --glob=heads/*' '

	compare rev-list "main other/three someref subspace-x subspace/one subspace/two" "--glob=heads/*"

'

test_expect_success 'rev-list --tags=foo' '

	compare rev-list "foo/bar" "--tags=foo"

'

test_expect_success 'rev-list --tags' '

	compare rev-list "foo/bar qux/x qux/two qux/one" "--tags"

'

test_expect_success 'rev-list --remotes=foo' '

	compare rev-list "foo/baz" "--remotes=foo"

'

test_expect_success 'rev-list --exclude with --branches' '
	compare rev-list "--exclude=*/* --branches" "main someref subspace-x"
'

test_expect_success 'rev-list --exclude with --all' '
	compare rev-list "--exclude=refs/remotes/* --all" "--branches --tags"
'

test_expect_success 'rev-list accumulates multiple --exclude' '
	compare rev-list "--exclude=refs/remotes/* --exclude=refs/tags/* --all" --branches
'

test_expect_success 'rev-list should succeed with empty output on empty stdin' '
	but rev-list --stdin </dev/null >actual &&
	test_must_be_empty actual
'

test_expect_success 'rev-list should succeed with empty output with all refs excluded' '
	but rev-list --exclude=* --all >actual &&
	test_must_be_empty actual
'

test_expect_success 'rev-list should succeed with empty output with empty --all' '
	(
		test_create_repo empty &&
		cd empty &&
		but rev-list --all >actual &&
		test_must_be_empty actual
	)
'

test_expect_success 'rev-list should succeed with empty output with empty glob' '
	but rev-list --glob=does-not-match-anything >actual &&
	test_must_be_empty actual
'

test_expect_success 'rev-list should succeed with empty output when ignoring missing' '
	but rev-list --ignore-missing $ZERO_OID >actual &&
	test_must_be_empty actual
'

test_expect_success 'shortlog accepts --glob/--tags/--remotes' '

	compare shortlog "subspace/one subspace/two" --branches=subspace &&
	compare shortlog \
	  "main subspace-x someref other/three subspace/one subspace/two" \
	  --branches &&
	compare shortlog main "--glob=heads/someref/* main" &&
	compare shortlog "subspace/one subspace/two other/three" \
	  "--glob=heads/subspace/* --glob=heads/other/*" &&
	compare shortlog \
	  "main other/three someref subspace-x subspace/one subspace/two" \
	  "--glob=heads/*" &&
	compare shortlog foo/bar --tags=foo &&
	compare shortlog "foo/bar qux/one qux/two qux/x" --tags &&
	compare shortlog foo/baz --remotes=foo

'

test_expect_failure 'shortlog accepts --glob as detached option' '

	compare shortlog \
	  "main other/three someref subspace-x subspace/one subspace/two" \
	  "--glob heads/*"

'

test_expect_failure 'shortlog --glob is not confused by option-like argument' '

	compare shortlog main "--glob -e main"

'

test_done
