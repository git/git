#!/bin/bash

test_description='config-managed multihooks, including git-hook command'

. ./test-lib.sh

test_expect_success 'git hook command does not crash' '
	git hook
'

test_done
