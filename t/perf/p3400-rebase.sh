#!/bin/sh

test_description='Tests rebase performance'
. ./perf-lib.sh

test_perf_default_repo

test_expect_success 'setup rebasing on top of a lot of changes' '
	but checkout -f -B base &&
	but checkout -B to-rebase &&
	but checkout -B upstream &&
	for i in $(test_seq 100)
	do
		# simulate huge diffs
		echo change$i >unrelated-file$i &&
		test_seq 1000 >>unrelated-file$i &&
		but add unrelated-file$i &&
		test_tick &&
		but cummit -m cummit$i unrelated-file$i &&
		echo change$i >unrelated-file$i &&
		test_seq 1000 | sort -nr >>unrelated-file$i &&
		but add unrelated-file$i &&
		test_tick &&
		but cummit -m cummit$i-reverse unrelated-file$i ||
		return 1
	done &&
	but checkout to-rebase &&
	test_cummit our-patch interesting-file
'

test_perf 'rebase on top of a lot of unrelated changes' '
	but rebase --onto upstream HEAD^ &&
	but rebase --onto base HEAD^
'

test_expect_success 'setup rebasing many changes without split-index' '
	but config core.splitIndex false &&
	but checkout -B upstream2 to-rebase &&
	but checkout -B to-rebase2 upstream
'

test_perf 'rebase a lot of unrelated changes without split-index' '
	but rebase --onto upstream2 base &&
	but rebase --onto base upstream2
'

test_expect_success 'setup rebasing many changes with split-index' '
	but config core.splitIndex true
'

test_perf 'rebase a lot of unrelated changes with split-index' '
	but rebase --onto upstream2 base &&
	but rebase --onto base upstream2
'

test_done
