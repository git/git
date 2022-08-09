#!/bin/sh

test_description='test fetching bundles with --bundle-uri'

. ./test-lib.sh

test_expect_success 'fail to clone from non-existent file' '
	test_when_finished rm -rf test &&
	git clone --bundle-uri="$(pwd)/does-not-exist" . test 2>err &&
	grep "failed to download bundle from URI" err
'

test_expect_success 'fail to clone from non-bundle file' '
	test_when_finished rm -rf test &&
	echo bogus >bogus &&
	git clone --bundle-uri="$(pwd)/bogus" . test 2>err &&
	grep "is not a bundle" err
'

test_expect_success 'create bundle' '
	git init clone-from &&
	git -C clone-from checkout -b topic &&
	test_commit -C clone-from A &&
	test_commit -C clone-from B &&
	git -C clone-from bundle create B.bundle topic
'

test_expect_success 'clone with path bundle' '
	git clone --bundle-uri="clone-from/B.bundle" \
		clone-from clone-path &&
	git -C clone-path rev-parse refs/bundles/topic >actual &&
	git -C clone-from rev-parse topic >expect &&
	test_cmp expect actual
'

test_done
