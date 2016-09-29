#!/bin/sh
#
# Copyright (c) 2016 Johannes Schindelin
#

test_description='Pickaxe options'

. ./test-lib.sh

test_expect_success setup '
	test_commit initial &&
	printf "%04096d" 0 >4096-zeroes.txt &&
	git add 4096-zeroes.txt &&
	test_tick &&
	git commit -m "A 4k file"
'
test_expect_success '-G matches' '
	git diff --name-only -G "^0{4096}$" HEAD^ >out &&
	test 4096-zeroes.txt = "$(cat out)"
'

test_done
