#!/bin/sh

test_description="test fetching through http proxy"

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-httpd.sh

LIB_HTTPD_PROXY=1
start_httpd

test_expect_success 'setup repository' '
	test_commit foo &&
	git init --bare "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	git push --mirror "$HTTPD_DOCUMENT_ROOT_PATH/repo.git"
'

setup_askpass_helper

# sanity check that our test setup is correctly using proxy
test_expect_success 'proxy requires password' '
	test_config_global http.proxy $HTTPD_DEST &&
	test_must_fail git clone $HTTPD_URL/smart/repo.git 2>err &&
	grep "error.*407" err
'

test_expect_success 'clone through proxy with auth' '
	test_when_finished "rm -rf clone" &&
	test_config_global http.proxy http://proxuser:proxpass@$HTTPD_DEST &&
	GIT_TRACE_CURL=$PWD/trace git clone $HTTPD_URL/smart/repo.git clone &&
	grep -i "Proxy-Authorization: Basic <redacted>" trace
'

test_expect_success 'clone can prompt for proxy password' '
	test_when_finished "rm -rf clone" &&
	test_config_global http.proxy http://proxuser@$HTTPD_DEST &&
	set_askpass nobody proxpass &&
	GIT_TRACE_CURL=$PWD/trace git clone $HTTPD_URL/smart/repo.git clone &&
	expect_askpass pass proxuser
'

test_done
