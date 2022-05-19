#!/bin/sh

test_description="Tests history walking performance"

. ./perf-lib.sh

test_perf_default_repo

test_perf 'rev-list --all' '
	but rev-list --all >/dev/null
'

test_perf 'rev-list --all --objects' '
	but rev-list --all --objects >/dev/null
'

test_perf 'rev-list --parents' '
	but rev-list --parents HEAD >/dev/null
'

test_expect_success 'create dummy file' '
	echo unlikely-to-already-be-there >dummy &&
	but add dummy &&
	but cummit -m dummy
'

test_perf 'rev-list -- dummy' '
	but rev-list HEAD -- dummy
'

test_perf 'rev-list --parents -- dummy' '
	but rev-list --parents HEAD -- dummy
'

test_expect_success 'create new unreferenced cummit' '
	cummit=$(but cummit-tree HEAD^{tree} -p HEAD) &&
	test_export cummit
'

test_perf 'rev-list $cummit --not --all' '
	but rev-list $cummit --not --all >/dev/null
'

test_perf 'rev-list --objects $cummit --not --all' '
	but rev-list --objects $cummit --not --all >/dev/null
'

test_done
