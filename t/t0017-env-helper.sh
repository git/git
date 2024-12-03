#!/bin/sh

test_description='test test-tool env-helper'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh


test_expect_success 'test-tool env-helper usage' '
	test_must_fail test-tool env-helper &&
	test_must_fail test-tool env-helper --type=bool &&
	test_must_fail test-tool env-helper --type=ulong &&
	test_must_fail test-tool env-helper --type=bool &&
	test_must_fail test-tool env-helper --type=bool --default &&
	test_must_fail test-tool env-helper --type=bool --default= &&
	test_must_fail test-tool env-helper --defaultxyz
'

test_expect_success 'test-tool env-helper bad default values' '
	test_must_fail test-tool env-helper --type=bool --default=1xyz MISSING &&
	test_must_fail test-tool env-helper --type=ulong --default=1xyz MISSING
'

test_expect_success 'test-tool env-helper --type=bool' '
	# Test various --default bool values
	echo true >expected &&
	test-tool env-helper --type=bool --default=1 MISSING >actual &&
	test_cmp expected actual &&
	test-tool env-helper --type=bool --default=yes MISSING >actual &&
	test_cmp expected actual &&
	test-tool env-helper --type=bool --default=true MISSING >actual &&
	test_cmp expected actual &&
	echo false >expected &&
	test_must_fail test-tool env-helper --type=bool --default=0 MISSING >actual &&
	test_cmp expected actual &&
	test_must_fail test-tool env-helper --type=bool --default=no MISSING >actual &&
	test_cmp expected actual &&
	test_must_fail test-tool env-helper --type=bool --default=false MISSING >actual &&
	test_cmp expected actual &&

	# No output with --exit-code
	test-tool env-helper --type=bool --default=true --exit-code MISSING >actual.out 2>actual.err &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err &&
	test_must_fail test-tool env-helper --type=bool --default=false --exit-code MISSING >actual.out 2>actual.err &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err &&

	# Existing variable
	EXISTS=true test-tool env-helper --type=bool --default=false --exit-code EXISTS >actual.out 2>actual.err &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err &&
	test_must_fail \
		env EXISTS=false \
		test-tool env-helper --type=bool --default=true --exit-code EXISTS >actual.out 2>actual.err &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success 'test-tool env-helper --type=ulong' '
	echo 1234567890 >expected &&
	test-tool env-helper --type=ulong --default=1234567890 MISSING >actual.out 2>actual.err &&
	test_cmp expected actual.out &&
	test_must_be_empty actual.err &&

	echo 0 >expected &&
	test_must_fail test-tool env-helper --type=ulong --default=0 MISSING >actual &&
	test_cmp expected actual &&

	test-tool env-helper --type=ulong --default=1234567890 --exit-code MISSING >actual.out 2>actual.err &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err &&

	EXISTS=1234567890 test-tool env-helper --type=ulong --default=0 EXISTS --exit-code >actual.out 2>actual.err &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err &&

	echo 1234567890 >expected &&
	EXISTS=1234567890 test-tool env-helper --type=ulong --default=0 EXISTS >actual.out 2>actual.err &&
	test_cmp expected actual.out &&
	test_must_be_empty actual.err
'

test_expect_success 'test-tool env-helper reads config thanks to trace2' '
	mkdir home &&
	git config -f home/.gitconfig include.path cycle &&
	git config -f home/cycle include.path .gitconfig &&

	test_must_fail \
		env HOME="$(pwd)/home" \
		git config -l 2>err &&
	grep "exceeded maximum include depth" err &&

	# This validates that the assumption that we attempt to
	# read the configuration and fail very early in the start-up
	# sequence (due to trace2 subsystem), even before we notice
	# that the directory named with "test-tool -C" does not exist
	# and die.  It is a dubious thing to test, though.
	test_must_fail \
		env HOME="$(pwd)/home" GIT_TEST_ENV_HELPER=true \
		test-tool -C no-such-directory \
		env-helper --type=bool --default=0 \
		--exit-code GIT_TEST_ENV_HELPER 2>err &&
	grep "exceeded maximum include depth" err
'

test_done
