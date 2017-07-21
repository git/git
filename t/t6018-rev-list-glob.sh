#!/bin/sh

test_description='rev-list/rev-parse --glob'

. ./test-lib.sh

commit () {
	test_tick &&
	echo $1 > foo &&
	git add foo &&
	git commit -m "$1"
}

compare () {
	# Split arguments on whitespace.
	git $1 $2 >expected &&
	git $1 $3 >actual &&
	test_cmp expected actual
}

test_expect_success 'setup' '

	commit master &&
	git checkout -b subspace/one master &&
	commit one &&
	git checkout -b subspace/two master &&
	commit two &&
	git checkout -b subspace-x master &&
	commit subspace-x &&
	git checkout -b other/three master &&
	commit three &&
	git checkout -b someref master &&
	commit some &&
	git checkout master &&
	commit master2 &&
	git tag foo/bar master &&
	commit master3 &&
	git update-ref refs/remotes/foo/baz master &&
	commit master4
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

	compare rev-parse "master" "--glob --symbolic master"

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

test_expect_success 'rev-parse --glob=heads/someref/* master' '

	compare rev-parse "master" "--glob=heads/someref/* master"

'

test_expect_success 'rev-parse --glob=heads/*' '

	compare rev-parse "master other/three someref subspace-x subspace/one subspace/two" "--glob=heads/*"

'

test_expect_success 'rev-parse --tags=foo' '

	compare rev-parse "foo/bar" "--tags=foo"

'

test_expect_success 'rev-parse --remotes=foo' '

	compare rev-parse "foo/baz" "--remotes=foo"

'

test_expect_success 'rev-parse --exclude with --branches' '
	compare rev-parse "--exclude=*/* --branches" "master someref subspace-x"
'

test_expect_success 'rev-parse --exclude with --all' '
	compare rev-parse "--exclude=refs/remotes/* --all" "--branches --tags"
'

test_expect_success 'rev-parse accumulates multiple --exclude' '
	compare rev-parse "--exclude=refs/remotes/* --exclude=refs/tags/* --all" --branches
'

test_expect_success 'rev-list --glob=refs/heads/subspace/*' '

	compare rev-list "subspace/one subspace/two" "--glob=refs/heads/subspace/*"

'

test_expect_success 'rev-list --glob refs/heads/subspace/*' '

	compare rev-list "subspace/one subspace/two" "--glob refs/heads/subspace/*"

'

test_expect_success 'rev-list not confused by option-like --glob arg' '

	compare rev-list "master" "--glob -0 master"

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

	compare rev-list "master subspace-x someref other/three subspace/one subspace/two" "--branches"

'

test_expect_success 'rev-list --glob=heads/someref/* master' '

	compare rev-list "master" "--glob=heads/someref/* master"

'

test_expect_success 'rev-list --glob=heads/subspace/* --glob=heads/other/*' '

	compare rev-list "subspace/one subspace/two other/three" "--glob=heads/subspace/* --glob=heads/other/*"

'

test_expect_success 'rev-list --glob=heads/*' '

	compare rev-list "master other/three someref subspace-x subspace/one subspace/two" "--glob=heads/*"

'

test_expect_success 'rev-list --tags=foo' '

	compare rev-list "foo/bar" "--tags=foo"

'

test_expect_success 'rev-list --tags' '

	compare rev-list "foo/bar" "--tags"

'

test_expect_success 'rev-list --remotes=foo' '

	compare rev-list "foo/baz" "--remotes=foo"

'

test_expect_success 'rev-list --exclude with --branches' '
	compare rev-list "--exclude=*/* --branches" "master someref subspace-x"
'

test_expect_success 'rev-list --exclude with --all' '
	compare rev-list "--exclude=refs/remotes/* --all" "--branches --tags"
'

test_expect_success 'rev-list accumulates multiple --exclude' '
	compare rev-list "--exclude=refs/remotes/* --exclude=refs/tags/* --all" --branches
'


# "git rev-list<ENTER>" is likely to be a bug in the calling script and may
# deserve an error message, but do cases where set of refs programmatically
# given using globbing and/or --stdin need to fail with the same error, or
# are we better off reporting a success with no output?  The following few
# tests document the current behaviour to remind us that we might want to
# think about this issue.

test_expect_failure 'rev-list may want to succeed with empty output on no input (1)' '
	>expect &&
	git rev-list --stdin <expect >actual &&
	test_cmp expect actual
'

test_expect_failure 'rev-list may want to succeed with empty output on no input (2)' '
	>expect &&
	git rev-list --exclude=* --all >actual &&
	test_cmp expect actual
'

test_expect_failure 'rev-list may want to succeed with empty output on no input (3)' '
	(
		test_create_repo empty &&
		cd empty &&
		>expect &&
		git rev-list --all >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'shortlog accepts --glob/--tags/--remotes' '

	compare shortlog "subspace/one subspace/two" --branches=subspace &&
	compare shortlog \
	  "master subspace-x someref other/three subspace/one subspace/two" \
	  --branches &&
	compare shortlog master "--glob=heads/someref/* master" &&
	compare shortlog "subspace/one subspace/two other/three" \
	  "--glob=heads/subspace/* --glob=heads/other/*" &&
	compare shortlog \
	  "master other/three someref subspace-x subspace/one subspace/two" \
	  "--glob=heads/*" &&
	compare shortlog foo/bar --tags=foo &&
	compare shortlog foo/bar --tags &&
	compare shortlog foo/baz --remotes=foo

'

test_expect_failure 'shortlog accepts --glob as detached option' '

	compare shortlog \
	  "master other/three someref subspace-x subspace/one subspace/two" \
	  "--glob heads/*"

'

test_expect_failure 'shortlog --glob is not confused by option-like argument' '

	compare shortlog master "--glob -e master"

'

test_done
