#!/bin/sh

test_description='blaming trough history with topic branches'
. ./test-lib.sh

# Creates the history shown below. '*'s mark the first parent in the merges.
# The only line of file.t is changed in cummit B2
#
#        +---C1
#       /      \
# A0--A1--*A2--*A3
#   \     /
#    B1-B2
#
test_expect_success setup '
	test_cummit A0 file.t line0 &&
	test_cummit A1 &&
	but reset --hard A0 &&
	test_cummit B1 &&
	test_cummit B2 file.t line0changed &&
	but reset --hard A1 &&
	test_merge A2 B2 &&
	but reset --hard A1 &&
	test_cummit C1 &&
	but reset --hard A2 &&
	test_merge A3 C1
	'

test_expect_success 'blame --reverse --first-parent finds A1' '
	but blame --porcelain --reverse --first-parent A0..A3 -- file.t >actual_full &&
	head -n 1 <actual_full | sed -e "s/ .*//" >actual &&
	but rev-parse A1 >expect &&
	test_cmp expect actual
	'

test_done
