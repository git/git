#!/bin/sh

test_description='but mergetool

Testing basic merge tools options'

. ./test-lib.sh

test_expect_success 'mergetool --tool=vimdiff creates the expected layout' '
	. $BUT_BUILD_DIR/mergetools/vimdiff &&
	run_unit_tests
'

test_done
