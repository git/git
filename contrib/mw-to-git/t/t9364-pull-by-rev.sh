#!/bin/sh

test_description='Test the Git Mediawiki remote helper: but pull by revision'

. ./test-butmw-lib.sh
. ./push-pull-tests.sh
. $TEST_DIRECTORY/test-lib.sh

test_check_precond

test_expect_success 'configuration' '
	but config --global mediawiki.fetchStrategy by_rev
'

test_push_pull

test_done
