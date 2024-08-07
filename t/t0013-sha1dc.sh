#!/bin/sh

test_description='test sha1 collision detection'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
TEST_DATA="$TEST_DIRECTORY/t0013"

test_lazy_prereq SHA1_IS_SHA1DC 'test-tool sha1-is-sha1dc'

if ! test_have_prereq SHA1_IS_SHA1DC
then
	skip_all='skipping sha1 collision tests, not using sha1collisiondetection'
	test_done
fi

test_expect_success 'test-sha1 detects shattered pdf' '
	test_must_fail test-tool sha1 <"$TEST_DATA/shattered-1.pdf" 2>err &&
	test_grep collision err &&
	grep 38762cf7f55934b34d179ae6a4c80cadccbb7f0a err
'

test_done
