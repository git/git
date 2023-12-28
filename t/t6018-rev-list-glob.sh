#!/bin/sh

test_description='rev-list/rev-parse --glob'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
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

	commit main &&
	git checkout -b subspace/one main &&
	commit one &&
	git checkout -b subspace/two main &&
	commit two &&
	git checkout -b subspace-x main &&
	commit subspace-x &&
	git checkout -b other/three main &&
	commit three &&
	git checkout -b someref main &&
	commit some &&
	git checkout main &&
	commit topic_2 &&
	git tag foo/bar main &&
	commit topic_3 &&
	git update-ref refs/remotes/foo/baz main &&
	commit topic_4 &&
	git update-ref refs/remotes/upstream/one subspace/one &&
	git update-ref refs/remotes/upstream/two subspace/two &&
	git update-ref refs/remotes/upstream/x subspace-x &&
	git tag qux/one subspace/one &&
	git tag qux/two subspace/two &&
	git tag qux/x subspace-x
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

for section in fetch receive uploadpack
do
	test_expect_success "rev-parse --exclude-hidden=$section with --all" '
		compare "-c transfer.hideRefs=refs/remotes/ rev-parse" "--branches --tags" "--exclude-hidden=$section --all"
	'

	test_expect_success "rev-parse --exclude-hidden=$section with --all" '
		compare "-c transfer.hideRefs=refs/heads/subspace/ rev-parse" "--exclude=refs/heads/subspace/* --all" "--exclude-hidden=$section --all"
	'

	test_expect_success "rev-parse --exclude-hidden=$section with --glob" '
		compare "-c transfer.hideRefs=refs/heads/subspace/ rev-parse" "--exclude=refs/heads/subspace/* --glob=refs/heads/*" "--exclude-hidden=$section --glob=refs/heads/*"
	'

	test_expect_success "rev-parse --exclude-hidden=$section can be passed once per pseudo-ref" '
		compare "-c transfer.hideRefs=refs/remotes/ rev-parse" "--branches --tags --branches --tags" "--exclude-hidden=$section --all --exclude-hidden=$section --all"
	'

	test_expect_success "rev-parse --exclude-hidden=$section can only be passed once per pseudo-ref" '
		echo "fatal: --exclude-hidden= passed more than once" >expected &&
		test_must_fail git rev-parse --exclude-hidden=$section --exclude-hidden=$section 2>err &&
		test_cmp expected err
	'

	for pseudoopt in branches tags remotes
	do
		test_expect_success "rev-parse --exclude-hidden=$section fails with --$pseudoopt" '
			test_must_fail git rev-parse --exclude-hidden=$section --$pseudoopt 2>err &&
			test_grep "error: options .--exclude-hidden. and .--$pseudoopt. cannot be used together" err
		'

		test_expect_success "rev-parse --exclude-hidden=$section fails with --$pseudoopt=pattern" '
			test_must_fail git rev-parse --exclude-hidden=$section --$pseudoopt=pattern 2>err &&
			test_grep "error: options .--exclude-hidden. and .--$pseudoopt. cannot be used together" err
		'
	done
done

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
	git rev-list --stdin </dev/null >actual &&
	test_must_be_empty actual
'

test_expect_success 'rev-list should succeed with empty output with all refs excluded' '
	git rev-list --exclude=* --all >actual &&
	test_must_be_empty actual
'

test_expect_success 'rev-list should succeed with empty output with empty --all' '
	(
		test_create_repo empty &&
		cd empty &&
		git rev-list --all >actual &&
		test_must_be_empty actual
	)
'

test_expect_success 'rev-list should succeed with empty output with empty glob' '
	git rev-list --glob=does-not-match-anything >actual &&
	test_must_be_empty actual
'

test_expect_success 'rev-list should succeed with empty output when ignoring missing' '
	git rev-list --ignore-missing $ZERO_OID >actual &&
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
