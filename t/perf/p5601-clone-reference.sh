#!/bin/sh

test_description='speed of clone --reference'
. ./perf-lib.sh

test_perf_default_repo

test_expect_success 'create shareable repository' '
	git clone --bare . shared.git
'

test_expect_success 'advance base repository' '
	# Do not use test_commit here; its test_tick will
	# use some ancient hard-coded date. The resulting clock
	# skew will cause pack-objects to traverse in a very
	# sub-optimal order, skewing the results.
	echo content >new-file-that-does-not-exist &&
	git add new-file-that-does-not-exist &&
	git commit -m "new commit"
'

test_perf 'clone --reference' '
	rm -rf dst.git &&
	git clone --no-local --bare --reference shared.git . dst.git
'

test_done
