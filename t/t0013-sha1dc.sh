#!/bin/sh

test_description='test sha1 collision detection'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
TEST_DATA="$TEST_DIRECTORY/t0013"

if test -z "$DC_SHA1"
then
	skip_all='skipping sha1 collision tests, DC_SHA1 not set'
	test_done
fi

test_expect_success 'test-sha1 detects shattered pdf' '
	test_must_fail test-tool sha1 <"$TEST_DATA/shattered-1.pdf" 2>err &&
	test_i18ngrep collision err &&
	grep 38762cf7f55934b34d179ae6a4c80cadccbb7f0a err
'

test_done
