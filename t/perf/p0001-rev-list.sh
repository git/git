#!/bin/sh

test_description="Tests history walking performance"

. ./perf-lib.sh

test_perf_default_repo

test_perf 'rev-list --all' '
	git rev-list --all >/dev/null
'

test_perf 'rev-list --all --objects' '
	git rev-list --all --objects >/dev/null
'

test_perf 'rev-list --parents' '
	git rev-list --parents HEAD >/dev/null
'

test_expect_success 'create dummy file' '
	echo unlikely-to-already-be-there >dummy &&
	git add dummy &&
	git commit -m dummy
'

test_perf 'rev-list -- dummy' '
	git rev-list HEAD -- dummy
'

test_perf 'rev-list --parents -- dummy' '
	git rev-list --parents HEAD -- dummy
'

test_expect_success 'create new unreferenced commit' '
	commit=$(git commit-tree HEAD^{tree} -p HEAD) &&
	test_export commit
'

test_perf 'rev-list $commit --not --all' '
	git rev-list $commit --not --all >/dev/null
'

test_perf 'rev-list --objects $commit --not --all' '
	git rev-list --objects $commit --not --all >/dev/null
'

test_done
