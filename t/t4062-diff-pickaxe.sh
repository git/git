#!/bin/sh
#
# Copyright (c) 2016 Johannes Schindelin
#

test_description='Pickaxe options'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	test_cummit initial &&
	printf "%04096d" 0 >4096-zeroes.txt &&
	but add 4096-zeroes.txt &&
	test_tick &&
	but cummit -m "A 4k file"
'

# OpenBSD only supports up to 255 repetitions, so repeat twice for 64*64=4096.
test_expect_success '-G matches' '
	but diff --name-only -G "^(0{64}){64}$" HEAD^ >out &&
	test 4096-zeroes.txt = "$(cat out)"
'

test_expect_success '-S --pickaxe-regex' '
	but diff --name-only -S0 --pickaxe-regex HEAD^ >out &&
	verbose test 4096-zeroes.txt = "$(cat out)"
'

test_done
