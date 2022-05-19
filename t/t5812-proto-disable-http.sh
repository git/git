#!/bin/sh

test_description='test disabling of but-over-http in clone/fetch'
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-proto-disable.sh"
. "$TEST_DIRECTORY/lib-httpd.sh"
start_httpd

test_expect_success 'create but-accessible repo' '
	bare="$HTTPD_DOCUMENT_ROOT_PATH/repo.but" &&
	test_cummit one &&
	but --bare init "$bare" &&
	but push "$bare" HEAD &&
	but -C "$bare" config http.receivepack true
'

test_proto "smart http" http "$HTTPD_URL/smart/repo.but"

test_expect_success 'curl redirects respect whitelist' '
	test_must_fail env BUT_ALLOW_PROTOCOL=http:https \
			   BUT_SMART_HTTP=0 \
		but clone "$HTTPD_URL/ftp-redir/repo.but" 2>stderr &&
	test_i18ngrep -E "(ftp.*disabled|your curl version is too old)" stderr
'

test_expect_success 'curl limits redirects' '
	test_must_fail but clone "$HTTPD_URL/loop-redir/smart/repo.but"
'

test_expect_success 'http can be limited to from-user' '
	but -c protocol.http.allow=user \
		clone "$HTTPD_URL/smart/repo.but" plain.but &&
	test_must_fail but -c protocol.http.allow=user \
		clone "$HTTPD_URL/smart-redir-perm/repo.but" redir.but
'

test_done
