#!/bin/sh
#
# Copyright (c) 2020 Google LLC
#

test_description='reftable basics'

. ./test-lib.sh

test_expect_success 'basic operation of reftable storage' '
	git init --ref-storage=reftable repo && (
	cd repo &&
	echo "hello" >world.txt &&
	git add world.txt &&
	git commit -m "first post" &&
	test_write_lines HEAD refs/heads/master >expect &&
	git show-ref &&
	git show-ref | cut -f2 -d" " > actual &&
	test_cmp actual expect &&
	for count in $(test_seq 1 10)
	do
		echo "hello" >>world.txt
		git commit -m "number ${count}" world.txt ||
		return 1
	done &&
	git gc &&
	nfiles=$(ls -1 .git/reftable | wc -l ) &&
	test ${nfiles} = "2" &&
	git reflog refs/heads/master >output &&
	test_line_count = 11 output &&
	grep "commit (initial): first post" output &&
	grep "commit: number 10" output )
'

test_done
