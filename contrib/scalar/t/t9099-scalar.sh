#!/bin/sh

test_description='test the `scalar` command'

TEST_DIRECTORY=$PWD/../../../t
export TEST_DIRECTORY

# Make it work with --no-bin-wrappers
PATH=$PWD/..:$PATH

. ../../../t/test-lib.sh

test_expect_success 'scalar shows a usage' '
	test_expect_code 129 scalar -h
'

test_expect_success 'scalar unregister' '
	git init vanish/src &&
	scalar register vanish/src &&
	git config --get --global --fixed-value \
		maintenance.repo "$(pwd)/vanish/src" &&
	scalar list >scalar.repos &&
	grep -F "$(pwd)/vanish/src" scalar.repos &&
	rm -rf vanish/src/.git &&
	scalar unregister vanish &&
	test_must_fail git config --get --global --fixed-value \
		maintenance.repo "$(pwd)/vanish/src" &&
	scalar list >scalar.repos &&
	! grep -F "$(pwd)/vanish/src" scalar.repos
'

test_done
