#!/bin/sh

test_description='Basic sort performance tests'
. ./perf-lib.sh

test_perf_default_repo

test_expect_success 'setup' '
	git rev-list --all >rla
'

test_perf 'cat-file --batch-check' '
	git cat-file --batch-check <rla
'

test_perf 'cat-file --batch-check with atoms' '
	git cat-file --batch-check="%(objectname) %(objecttype)" <rla
'

test_perf 'cat-file --batch' '
	git cat-file --batch <rla
'

test_perf 'cat-file --batch with atoms' '
	git cat-file --batch="%(objectname) %(objecttype)" <rla
'

test_done
