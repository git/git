#!/bin/sh

test_description='test across ref formats'

. ./test-lib.sh

test_expect_success 'extensions.refFormat requires core.repositoryFormatVersion=1' '
	test_when_finished rm -rf broken &&

	# Force sha1 to ensure GIT_TEST_DEFAULT_HASH does
	# not imply a value of core.repositoryFormatVersion.
	git init --object-format=sha1 broken &&
	git -C broken config extensions.refFormat files &&
	test_must_fail git -C broken status 2>err &&
	grep "repo version is 0, but v1-only extension found" err
'

test_expect_success 'invalid extensions.refFormat' '
	test_when_finished rm -rf broken &&
	git init broken &&
	git -C broken config core.repositoryFormatVersion 1 &&
	git -C broken config extensions.refFormat bogus &&
	test_must_fail git -C broken status 2>err &&
	grep "invalid value for '\''extensions.refFormat'\'': '\''bogus'\''" err
'

test_done
