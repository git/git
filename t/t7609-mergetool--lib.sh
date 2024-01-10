#!/bin/sh

test_description='git mergetool

Testing basic merge tools options'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'mergetool --tool=vimdiff creates the expected layout' '
	. "$GIT_BUILD_DIR"/mergetools/vimdiff &&
	run_unit_tests
'

test_done
