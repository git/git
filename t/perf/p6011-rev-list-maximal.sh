#!/bin/sh

test_description='Test --maximal-only and --independent options'

. ./perf-lib.sh

test_perf_default_repo

test_expect_success 'setup' '
	git for-each-ref --format="%(*objecttype) %(objecttype) %(objectname)" \
		"refs/heads/*" "refs/tags/*" |
		sed -n -e "s/^commit commit //p" -e "s/^ commit //p" |
		head -n 50 >commits &&
	git commit-graph write --reachable
'

test_perf 'merge-base --independent' '
	git merge-base --independent $(cat commits) >/dev/null
'

test_perf 'rev-list --maximal-only' '
	git rev-list --maximal-only $(cat commits) >/dev/null
'

test_perf 'rev-list --maximal-only --since' '
	git rev-list --maximal-only --since=2000-01-01 $(cat commits) >/dev/null
'

test_done
