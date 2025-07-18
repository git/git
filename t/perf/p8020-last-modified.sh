#!/bin/sh

test_description='last-modified perf tests'
. ./perf-lib.sh

test_perf_default_repo

test_perf 'top-level last-modified' '
	git last-modified HEAD
'

test_perf 'top-level recursive last-modified' '
	git last-modified -r HEAD
'

test_perf 'subdir last-modified' '
	path=$(git ls-tree HEAD | grep ^040000 | head -n 1 | cut -f2)
	git last-modified -r HEAD -- "$path"
'

test_done
