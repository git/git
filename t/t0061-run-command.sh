#!/bin/sh
#
# Copyright (c) 2009 Ilari Liusvaara
#

test_description='Test run command'

. ./test-lib.sh

test_expect_success 'start_command reports ENOENT' '
	test-run-command start-command-ENOENT ./does-not-exist
'

test_done
