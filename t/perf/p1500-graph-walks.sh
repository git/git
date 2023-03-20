#!/bin/sh

test_description='Commit walk performance tests'
. ./perf-lib.sh

test_perf_large_repo

test_expect_success 'setup' '
	git for-each-ref --format="%(refname)" "refs/heads/*" "refs/tags/*" >allrefs &&
	sort -r allrefs | head -n 50 >refs &&
	for ref in $(cat refs)
	do
		git branch -f ref-$ref $ref &&
		echo ref-$ref ||
		return 1
	done >branches &&
	for ref in $(cat refs)
	do
		git tag -f tag-$ref $ref &&
		echo tag-$ref ||
		return 1
	done >tags &&
	git commit-graph write --reachable
'

test_perf 'ahead-behind counts: git for-each-ref' '
	git for-each-ref --format="%(ahead-behind:HEAD)" --stdin <refs
'

test_perf 'ahead-behind counts: git branch' '
	xargs git branch -l --format="%(ahead-behind:HEAD)" <branches
'

test_perf 'ahead-behind counts: git tag' '
	xargs git tag -l --format="%(ahead-behind:HEAD)" <tags
'

test_perf 'ahead-behind counts: git rev-list' '
	for r in $(cat refs)
	do
		git rev-list --count "HEAD..$r" || return 1
	done
'

test_done
