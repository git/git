#!/bin/sh

test_description='Tests rebase performance'
. ./perf-lib.sh

test_perf_default_repo

test_expect_success 'setup rebasing on top of a lot of changes' '
	git checkout -f -B base &&
	git checkout -B to-rebase &&
	git checkout -B upstream &&
	for i in $(seq 100)
	do
		# simulate huge diffs
		echo change$i >unrelated-file$i &&
		seq 1000 >>unrelated-file$i &&
		git add unrelated-file$i &&
		test_tick &&
		git commit -m commit$i unrelated-file$i &&
		echo change$i >unrelated-file$i &&
		seq 1000 | tac >>unrelated-file$i &&
		git add unrelated-file$i &&
		test_tick &&
		git commit -m commit$i-reverse unrelated-file$i ||
		break
	done &&
	git checkout to-rebase &&
	test_commit our-patch interesting-file
'

test_perf 'rebase on top of a lot of unrelated changes' '
	git rebase --onto upstream HEAD^ &&
	git rebase --onto base HEAD^
'

test_expect_success 'setup rebasing many changes without split-index' '
	git config core.splitIndex false &&
	git checkout -B upstream2 to-rebase &&
	git checkout -B to-rebase2 upstream
'

test_perf 'rebase a lot of unrelated changes without split-index' '
	git rebase --onto upstream2 base &&
	git rebase --onto base upstream2
'

test_expect_success 'setup rebasing many changes with split-index' '
	git config core.splitIndex true
'

test_perf 'rebase a lot of unrelated changes with split-index' '
	git rebase --onto upstream2 base &&
	git rebase --onto base upstream2
'

test_done
