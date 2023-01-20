#!/bin/sh

test_description='test http auth header and credential helper interop'

TEST_NO_CREATE_REPO=1
. ./test-lib.sh

# Setup a repository
#
REPO_DIR="$TRASH_DIRECTORY"/repo

SERVER_LOG="$TRASH_DIRECTORY"/OUT.server.log

PATH="$GIT_BUILD_DIR/t/helper/:$PATH" && export PATH

test_expect_success 'setup repos' '
	test_create_repo "$REPO_DIR" &&
	git -C "$REPO_DIR" branch -M main
'

run_http_server_worker() {
	(
		cd "$REPO_DIR"
		test-http-server --worker "$@" 2>"$SERVER_LOG" | tr -d "\r"
	)
}

per_test_cleanup () {
	rm -f OUT.* &&
	rm -f IN.* &&
}

test_expect_success 'http auth server request parsing' '
	test_when_finished "per_test_cleanup" &&

	cat >auth.config <<-EOF &&
	[auth]
		allowAnonymous = true
	EOF

	echo "HTTP/1.1 400 Bad Request" >OUT.http400 &&
	echo "HTTP/1.1 200 OK" >OUT.http200 &&

	cat >IN.http.valid <<-EOF &&
	GET /info/refs HTTP/1.1
	Content-Length: 0
	EOF

	cat >IN.http.badfirstline <<-EOF &&
	/info/refs GET HTTP
	EOF

	cat >IN.http.badhttpver <<-EOF &&
	GET /info/refs HTTP/999.9
	EOF

	cat >IN.http.ltzlen <<-EOF &&
	GET /info/refs HTTP/1.1
	Content-Length: -1
	EOF

	cat >IN.http.badlen <<-EOF &&
	GET /info/refs HTTP/1.1
	Content-Length: not-a-number
	EOF

	cat >IN.http.overlen <<-EOF &&
	GET /info/refs HTTP/1.1
	Content-Length: 9223372036854775807
	EOF

	run_http_server_worker \
		--auth-config="$TRASH_DIRECTORY/auth.config" <IN.http.valid \
		| head -n1 >OUT.actual &&
	test_cmp OUT.http200 OUT.actual &&

	run_http_server_worker <IN.http.badfirstline | head -n1 >OUT.actual &&
	test_cmp OUT.http400 OUT.actual &&

	run_http_server_worker <IN.http.ltzlen | head -n1 >OUT.actual &&
	test_cmp OUT.http400 OUT.actual &&

	run_http_server_worker <IN.http.badlen | head -n1 >OUT.actual &&
	test_cmp OUT.http400 OUT.actual &&

	run_http_server_worker <IN.http.overlen | head -n1 >OUT.actual &&
	test_cmp OUT.http400 OUT.actual
'

test_done
