#!/bin/sh

test_description='Tests rebase performance'
. ./perf-lib.sh

test_perf_default_repo

test_expect_success 'setup' '
	git checkout -f -b base &&
	git checkout -b to-rebase &&
	git checkout -b upstream &&
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

test_done
