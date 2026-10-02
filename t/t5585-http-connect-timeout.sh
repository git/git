#!/bin/sh

test_description='test http.connecttimeoutms and GIT_HTTP_CONNECT_TIMEOUT_MS'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'setup repository' '
	test_commit initial &&
	git clone --bare . "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	git --git-dir="$HTTPD_DOCUMENT_ROOT_PATH/repo.git" config http.receivepack true
'

test_expect_success 'http.connecttimeoutms accepts a positive integer via config' '
	test_config http.connecttimeoutms 5000 &&
	git ls-remote "$HTTPD_URL/smart/repo.git" >output &&
	test_grep "refs/heads/" output
'

test_expect_success 'http.connecttimeoutms=0 is accepted (disables the option)' '
	test_config http.connecttimeoutms 0 &&
	git ls-remote "$HTTPD_URL/smart/repo.git" >output &&
	test_grep "refs/heads/" output
'

test_expect_success 'GIT_HTTP_CONNECT_TIMEOUT_MS env var is accepted' '
	GIT_HTTP_CONNECT_TIMEOUT_MS=5000 \
		git ls-remote "$HTTPD_URL/smart/repo.git" >output 2>err &&
	test_grep "refs/heads/" output &&
	test_grep ! . err
'

test_expect_success 'http.connecttimeoutms rejects non-numeric config value' '
	test_config http.connecttimeoutms not-a-number &&
	test_must_fail git ls-remote "$HTTPD_URL/smart/repo.git" 2>err &&
	test_grep "bad numeric config value .not-a-number. for .http\.connecttimeoutms." err
'

test_expect_success 'http.connecttimeoutms rejects empty config value' '
	test_config http.connecttimeoutms "" &&
	test_must_fail git ls-remote "$HTTPD_URL/smart/repo.git" 2>err &&
	test_grep "bad numeric config value" err
'

test_expect_success 'GIT_HTTP_CONNECT_TIMEOUT_MS warns on non-numeric value but succeeds' '
	GIT_HTTP_CONNECT_TIMEOUT_MS=not-a-number \
		git ls-remote "$HTTPD_URL/smart/repo.git" >output 2>err &&
	test_grep "refs/heads/" output &&
	test_grep "failed to parse GIT_HTTP_CONNECT_TIMEOUT_MS" err
'

test_expect_success 'GIT_HTTP_CONNECT_TIMEOUT_MS warns on empty value but succeeds' '
	GIT_HTTP_CONNECT_TIMEOUT_MS= \
		git ls-remote "$HTTPD_URL/smart/repo.git" >output 2>err &&
	test_grep "refs/heads/" output &&
	test_grep "failed to parse GIT_HTTP_CONNECT_TIMEOUT_MS" err
'

test_done
