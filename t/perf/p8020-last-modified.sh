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
	git ls-tree -d HEAD >subtrees &&
	path="$(head -n 1 subtrees | cut -f2)" &&
	git last-modified -r HEAD -- "$path"
'

test_done
