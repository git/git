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

test_expect_success 'clone with file:// bundle' '
	git clone --bundle-uri="file://$(pwd)/clone-from/B.bundle" \
		clone-from clone-file &&
	git -C clone-file rev-parse refs/bundles/topic >actual &&
	git -C clone-from rev-parse topic >expect &&
	test_cmp expect actual
'

#########################################################################
# HTTP tests begin here

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'fail to fetch from non-existent HTTP URL' '
	test_when_finished rm -rf test &&
	git clone --bundle-uri="$HTTPD_URL/does-not-exist" . test 2>err &&
	grep "failed to download bundle from URI" err
'

test_expect_success 'fail to fetch from non-bundle HTTP URL' '
	test_when_finished rm -rf test &&
	echo bogus >"$HTTPD_DOCUMENT_ROOT_PATH/bogus" &&
	git clone --bundle-uri="$HTTPD_URL/bogus" . test 2>err &&
	grep "is not a bundle" err
'

test_expect_success 'clone HTTP bundle' '
	cp clone-from/B.bundle "$HTTPD_DOCUMENT_ROOT_PATH/B.bundle" &&

	git clone --no-local --mirror clone-from \
		"$HTTPD_DOCUMENT_ROOT_PATH/fetch.git" &&

	git clone --bundle-uri="$HTTPD_URL/B.bundle" \
		"$HTTPD_URL/smart/fetch.git" clone-http &&
	git -C clone-http rev-parse refs/bundles/topic >actual &&
	git -C clone-from rev-parse topic >expect &&
	test_cmp expect actual &&

	test_config -C clone-http log.excludedecoration refs/bundle/
'

# Do not add tests here unless they use the HTTP server, as they will
# not run unless the HTTP dependencies exist.

test_done
