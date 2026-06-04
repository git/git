#!/bin/sh

test_description='performance of git-describe'
. ./perf-lib.sh

test_perf_default_repo

# clear out old tags and give us a known state
test_expect_success 'set up tags' '
	git for-each-ref --format="delete %(refname)" refs/tags >to-delete &&
	git update-ref --stdin <to-delete &&
	new=$(git rev-list -1000 HEAD | tail -n 1) &&
	git tag -m new new $new &&
	old=$(git rev-list       HEAD | tail -n 1) &&
	git tag -m old old $old
'

test_perf 'describe HEAD' '
	git describe HEAD
'

test_perf 'describe HEAD with one max candidate' '
	git describe --candidates=1 HEAD
'

test_perf 'describe HEAD with one tag' '
	git describe --match=new HEAD
'

test_done
