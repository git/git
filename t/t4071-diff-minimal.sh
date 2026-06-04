#!/bin/sh

test_description='minimal diff algorithm'

. ./test-lib.sh

test_expect_success 'minimal diff should not mark changes between changed lines' '
	test_write_lines x x x x >pre &&
	test_write_lines x x x A B C D x E F G >post &&
	test_expect_code 1 git diff --no-index --minimal pre post >diff &&
	test_grep ! ^[+-]x diff 
'

test_done
