#!/bin/sh

test_description='Tests diff --color-moved performance'
. ./perf-lib.sh

test_perf_default_repo

if ! git rev-parse --verify v2.29.0^{commit} >/dev/null
then
	skip_all='skipping because tag v2.29.0 was not found'
	test_done
fi

GIT_PAGER_IN_USE=1
test_export GIT_PAGER_IN_USE

test_perf 'diff --no-color-moved --no-color-moved-ws large change' '
	git diff --no-color-moved --no-color-moved-ws v2.28.0 v2.29.0
'

test_perf 'diff --color-moved --no-color-moved-ws large change' '
	git diff --color-moved=zebra --no-color-moved-ws v2.28.0 v2.29.0
'

test_perf 'diff --color-moved-ws=allow-indentation-change large change' '
	git diff --color-moved=zebra --color-moved-ws=allow-indentation-change \
		v2.28.0 v2.29.0
'

test_perf 'log --no-color-moved --no-color-moved-ws' '
	git log --no-color-moved --no-color-moved-ws --no-merges --patch \
		-n1000 v2.29.0
'

test_perf 'log --color-moved --no-color-moved-ws' '
	git log --color-moved=zebra --no-color-moved-ws --no-merges --patch \
		-n1000 v2.29.0
'

test_perf 'log --color-moved-ws=allow-indentation-change' '
	git log --color-moved=zebra --color-moved-ws=allow-indentation-change \
		--no-merges --patch -n1000 v2.29.0
'

test_done
