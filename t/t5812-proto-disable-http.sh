#!/bin/sh

test_description='test disabling of git-over-http in clone/fetch'
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-proto-disable.sh"
. "$TEST_DIRECTORY/lib-httpd.sh"
start_httpd

test_expect_success 'create git-accessible repo' '
	bare="$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	test_commit one &&
	git --bare init "$bare" &&
	git push "$bare" HEAD &&
	git -C "$bare" config http.receivepack true
'

test_proto "smart http" http "$HTTPD_URL/smart/repo.git"

test_expect_success 'curl redirects respect whitelist' '
	test_must_fail env GIT_ALLOW_PROTOCOL=http:https \
		git clone "$HTTPD_URL/ftp-redir/repo.git" 2>stderr &&
	{
		test_i18ngrep "ftp.*disabled" stderr ||
		test_i18ngrep "your curl version is too old"
	}
'

test_expect_success 'curl limits redirects' '
	test_must_fail git clone "$HTTPD_URL/loop-redir/smart/repo.git"
'

stop_httpd
test_done
