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

test_expect_success 'http(s) transport respects GIT_ALLOW_PROTOCOL' '
	test_must_fail env GIT_ALLOW_PROTOCOL=http:https \
			   GIT_SMART_HTTP=0 \
		git clone "$HTTPD_URL/ftp-redir/repo.git" 2>stderr &&
	test_grep -E "(ftp.*disabled|your curl version is too old)" stderr
'

test_expect_success 'curl limits redirects' '
	test_must_fail git clone "$HTTPD_URL/loop-redir/smart/repo.git"
'

test_expect_success 'http can be limited to from-user' '
	git -c protocol.http.allow=user \
		clone "$HTTPD_URL/smart/repo.git" plain.git &&
	test_must_fail git -c protocol.http.allow=user \
		clone "$HTTPD_URL/smart-redir-perm/repo.git" redir.git
'

test_done
