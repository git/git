#!/bin/sh

test_description='verification of stapled OCSP responses via http.sslVerifyStatus'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

LIB_HTTPD_OCSP=1
. "$TEST_DIRECTORY"/lib-httpd.sh

start_httpd
start_ocsp_responder

test_expect_success 'setup repository' '
	test_commit one &&
	git init --bare "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	git push "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" HEAD:refs/heads/main
'

# lib-httpd.sh exports GIT_SSL_NO_VERIFY, which would keep us from ever
# looking at the certificate. Trust our own CA instead.
with_ssl_verification () {
	(
		sane_unset GIT_SSL_NO_VERIFY &&
		GIT_SSL_CAINFO="$HTTPD_ROOT_PATH/ca.pem" "$@"
	)
}

test_expect_success SSL_VERIFYSTATUS 'certificate verification works against test CA' '
	with_ssl_verification git ls-remote "$HTTPD_URL/smart/repo.git" >actual &&
	test_line_count -gt 0 actual
'

test_expect_success SSL_VERIFYSTATUS 'fetch succeeds with stapled "good" OCSP response' '
	with_ssl_verification git -c http.sslVerifyStatus=true \
		ls-remote "$HTTPD_URL/smart/repo.git" >actual &&
	test_line_count -gt 0 actual
'

test_expect_success SSL_VERIFYSTATUS 'revoked certificate is rejected' '
	revoke_httpd_cert &&
	with_ssl_verification test_must_fail git -c http.sslVerifyStatus=true \
		ls-remote "$HTTPD_URL/smart/repo.git" 2>err &&
	test_grep -i -e "ocsp" -e "revocation" -e "revoked" -e "certificate status" err
'

# Depends on the certificate revoked by the preceding test.
test_expect_success SSL_VERIFYSTATUS 'revoked certificate is accepted without http.sslVerifyStatus' '
	with_ssl_verification git ls-remote "$HTTPD_URL/smart/repo.git" >actual &&
	test_line_count -gt 0 actual
'

test_done
