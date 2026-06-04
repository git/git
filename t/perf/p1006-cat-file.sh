#!/bin/sh

test_description='Tests listing object info performance'
. ./perf-lib.sh

test_perf_large_repo

test_perf 'cat-file --batch-check' '
	git cat-file --batch-all-objects --batch-check
'

test_perf 'list all objects (sorted)' '
	git cat-file --batch-all-objects --batch-check="%(objectname)"
'

test_perf 'list all objects (unsorted)' '
	git cat-file --batch-all-objects --batch-check="%(objectname)" \
		--unordered
'

test_perf 'list blobs' '
	git cat-file --batch-all-objects --batch-check="%(objectname)" \
		--unordered --filter=object:type=blob
'

test_done
