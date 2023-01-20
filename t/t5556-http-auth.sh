#!/bin/sh

test_description='test http auth header and credential helper interop'

TEST_NO_CREATE_REPO=1
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-credential-helper.sh

test_set_port GIT_TEST_HTTP_PROTOCOL_PORT

# Setup a repository
#
REPO_DIR="$TRASH_DIRECTORY"/repo

# Setup some lookback URLs where test-http-server will be listening.
# We will spawn it directly inside the repo directory, so we avoid
# any need to configure directory mappings etc - we only serve this
# repository from the root '/' of the server.
#
HOST_PORT=127.0.0.1:$GIT_TEST_HTTP_PROTOCOL_PORT
ORIGIN_URL=http://$HOST_PORT/

# The pid-file is created by test-http-server when it starts.
# The server will shutdown if/when we delete it (this is easier than
# killing it by PID).
#
PID_FILE="$TRASH_DIRECTORY"/pid-file.pid
SERVER_LOG="$TRASH_DIRECTORY"/OUT.server.log

PATH="$GIT_BUILD_DIR/t/helper/:$PATH" && export PATH

test_expect_success 'setup repos' '
	test_create_repo "$REPO_DIR" &&
	git -C "$REPO_DIR" branch -M main
'

setup_credential_helper

run_http_server_worker() {
	(
		cd "$REPO_DIR"
		test-http-server --worker "$@" 2>"$SERVER_LOG" | tr -d "\r"
	)
}

stop_http_server () {
	if ! test -f "$PID_FILE"
	then
		return 0
	fi
	#
	# The server will shutdown automatically when we delete the pid-file.
	#
	rm -f "$PID_FILE"
	#
	# Give it a few seconds to shutdown (mainly to completely release the
	# port before the next test start another instance and it attempts to
	# bind to it).
	#
	for k in 0 1 2 3 4
	do
		if grep -q "Starting graceful shutdown" "$SERVER_LOG"
		then
			return 0
		fi
		sleep 1
	done

	echo "stop_http_server: timeout waiting for server shutdown"
	return 1
}

start_http_server () {
	#
	# Launch our server into the background in repo_dir.
	#
	(
		cd "$REPO_DIR"
		test-http-server --verbose \
			--listen=127.0.0.1 \
			--port=$GIT_TEST_HTTP_PROTOCOL_PORT \
			--reuseaddr \
			--pid-file="$PID_FILE" \
			"$@" \
			2>"$SERVER_LOG" &
	)
	#
	# Give it a few seconds to get started.
	#
	for k in 0 1 2 3 4
	do
		if test -f "$PID_FILE"
		then
			return 0
		fi
		sleep 1
	done

	echo "start_http_server: timeout waiting for server startup"
	return 1
}

per_test_cleanup () {
	stop_http_server &&
	rm -f OUT.* &&
	rm -f IN.* &&
	rm -f *.cred &&
	rm -f auth.config
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

test_expect_success CURL 'http auth server auth config' '
	test_when_finished "per_test_cleanup" &&

	cat >auth.config <<-EOF &&
	[auth]
		challenge = no-params
		challenge = with-params:foo=\"bar\" p=1
		challenge = with-params:foo=\"replaced\" q=1

		token = no-explicit-challenge:valid-token
		token = no-explicit-challenge:also-valid
		token = reset-tokens:these-tokens
		token = reset-tokens:will-be-reset
		token = reset-tokens:
		token = reset-tokens:the-only-valid-one

		allowAnonymous = false

		extraHeader = X-Extra-Header: abc
		extraHeader = X-Extra-Header: 123
		extraHeader = X-Another: header\twith\twhitespace!
	EOF

	cat >OUT.expected <<-EOF &&
	WWW-Authenticate: no-params
	WWW-Authenticate: with-params foo="replaced" q=1
	WWW-Authenticate: no-explicit-challenge
	WWW-Authenticate: reset-tokens
	X-Extra-Header: abc
	X-Extra-Header: 123
	X-Another: header	with	whitespace!

	Error: 401 Unauthorized
	EOF

	start_http_server --auth-config="$TRASH_DIRECTORY/auth.config" &&

	curl --include $ORIGIN_URL >OUT.curl &&
	tr -d "\r" <OUT.curl | sed -n "/WWW-Authenticate/,\$p" >OUT.actual &&

	test_cmp OUT.expected OUT.actual
'

test_expect_success 'http auth anonymous no challenge' '
	test_when_finished "per_test_cleanup" &&

	cat >auth.config <<-EOF &&
	[auth]
		allowAnonymous = true
	EOF

	start_http_server --auth-config="$TRASH_DIRECTORY/auth.config" &&

	# Attempt to read from a protected repository
	git ls-remote $ORIGIN_URL
'

test_expect_success 'http auth www-auth headers to credential helper basic valid' '
	test_when_finished "per_test_cleanup" &&
	# base64("alice:secret-passwd")
	USERPASS64=YWxpY2U6c2VjcmV0LXBhc3N3ZA== &&
	export USERPASS64 &&

	cat >auth.config <<-EOF &&
	[auth]
		challenge = basic:realm=\"example.com\"
		token = basic:$USERPASS64
	EOF

	start_http_server --auth-config="$TRASH_DIRECTORY/auth.config" &&

	set_credential_reply get <<-EOF &&
	protocol=http
	host=$HOST_PORT
	username=alice
	password=secret-passwd
	EOF

	git -c "credential.helper=!\"$CREDENTIAL_HELPER\"" ls-remote $ORIGIN_URL &&

	expect_credential_query get <<-EOF &&
	protocol=http
	host=$HOST_PORT
	wwwauth[]=basic realm="example.com"
	EOF

	expect_credential_query store <<-EOF
	protocol=http
	host=$HOST_PORT
	username=alice
	password=secret-passwd
	EOF
'

test_expect_success 'http auth www-auth headers to credential helper ignore case valid' '
	test_when_finished "per_test_cleanup" &&
	# base64("alice:secret-passwd")
	USERPASS64=YWxpY2U6c2VjcmV0LXBhc3N3ZA== &&
	export USERPASS64 &&

	cat >auth.config <<-EOF &&
	[auth]
		challenge = basic:realm=\"example.com\"
		token = basic:$USERPASS64
		extraHeader = wWw-aUtHeNtIcAtE: bEaRer auThoRiTy=\"id.example.com\"
	EOF

	start_http_server --auth-config="$TRASH_DIRECTORY/auth.config" &&

	set_credential_reply get <<-EOF &&
	protocol=http
	host=$HOST_PORT
	username=alice
	password=secret-passwd
	EOF

	git -c "credential.helper=!\"$CREDENTIAL_HELPER\"" ls-remote $ORIGIN_URL &&

	expect_credential_query get <<-EOF &&
	protocol=http
	host=$HOST_PORT
	wwwauth[]=basic realm="example.com"
	wwwauth[]=bEaRer auThoRiTy="id.example.com"
	EOF

	expect_credential_query store <<-EOF
	protocol=http
	host=$HOST_PORT
	username=alice
	password=secret-passwd
	EOF
'

test_expect_success 'http auth www-auth headers to credential helper continuation hdr' '
	test_when_finished "per_test_cleanup" &&
	# base64("alice:secret-passwd")
	USERPASS64=YWxpY2U6c2VjcmV0LXBhc3N3ZA== &&
	export USERPASS64 &&

	cat >auth.config <<-EOF &&
	[auth]
		challenge = "bearer:authority=\"id.example.com\"\\n    q=1\\n \\t p=0"
		challenge = basic:realm=\"example.com\"
		token = basic:$USERPASS64
	EOF

	start_http_server --auth-config="$TRASH_DIRECTORY/auth.config" &&

	set_credential_reply get <<-EOF &&
	protocol=http
	host=$HOST_PORT
	username=alice
	password=secret-passwd
	EOF

	git -c "credential.helper=!\"$CREDENTIAL_HELPER\"" ls-remote $ORIGIN_URL &&

	expect_credential_query get <<-EOF &&
	protocol=http
	host=$HOST_PORT
	wwwauth[]=bearer authority="id.example.com" q=1 p=0
	wwwauth[]=basic realm="example.com"
	EOF

	expect_credential_query store <<-EOF
	protocol=http
	host=$HOST_PORT
	username=alice
	password=secret-passwd
	EOF
'

test_expect_success 'http auth www-auth headers to credential helper empty continuation hdrs' '
	test_when_finished "per_test_cleanup" &&
	# base64("alice:secret-passwd")
	USERPASS64=YWxpY2U6c2VjcmV0LXBhc3N3ZA== &&
	export USERPASS64 &&

	cat >auth.config <<-EOF &&
	[auth]
		challenge = basic:realm=\"example.com\"
		token = basic:$USERPASS64
		extraheader = "WWW-Authenticate:"
		extraheader = " "
		extraheader = " bearer authority=\"id.example.com\""
	EOF

	start_http_server --auth-config="$TRASH_DIRECTORY/auth.config" &&

	set_credential_reply get <<-EOF &&
	protocol=http
	host=$HOST_PORT
	username=alice
	password=secret-passwd
	EOF

	git -c "credential.helper=!\"$CREDENTIAL_HELPER\"" ls-remote $ORIGIN_URL &&

	expect_credential_query get <<-EOF &&
	protocol=http
	host=$HOST_PORT
	wwwauth[]=basic realm="example.com"
	wwwauth[]=bearer authority="id.example.com"
	EOF

	expect_credential_query store <<-EOF
	protocol=http
	host=$HOST_PORT
	username=alice
	password=secret-passwd
	EOF
'

test_expect_success 'http auth www-auth headers to credential helper custom schemes' '
	test_when_finished "per_test_cleanup" &&
	# base64("alice:secret-passwd")
	USERPASS64=YWxpY2U6c2VjcmV0LXBhc3N3ZA== &&
	export USERPASS64 &&

	cat >auth.config <<-EOF &&
	[auth]
		challenge = "foobar:alg=test widget=1"
		challenge = "bearer:authority=\"id.example.com\" q=1 p=0"
		challenge = basic:realm=\"example.com\"
		token = basic:$USERPASS64
	EOF

	start_http_server --auth-config="$TRASH_DIRECTORY/auth.config" &&

	set_credential_reply get <<-EOF &&
	protocol=http
	host=$HOST_PORT
	username=alice
	password=secret-passwd
	EOF

	git -c "credential.helper=!\"$CREDENTIAL_HELPER\"" ls-remote $ORIGIN_URL &&

	expect_credential_query get <<-EOF &&
	protocol=http
	host=$HOST_PORT
	wwwauth[]=foobar alg=test widget=1
	wwwauth[]=bearer authority="id.example.com" q=1 p=0
	wwwauth[]=basic realm="example.com"
	EOF

	expect_credential_query store <<-EOF
	protocol=http
	host=$HOST_PORT
	username=alice
	password=secret-passwd
	EOF
'

test_expect_success 'http auth www-auth headers to credential helper invalid' '
	test_when_finished "per_test_cleanup" &&
	# base64("alice:secret-passwd")
	USERPASS64=YWxpY2U6c2VjcmV0LXBhc3N3ZA== &&
	export USERPASS64 &&

	cat >auth.config <<-EOF &&
	[auth]
		challenge = "bearer:authority=\"id.example.com\" q=1 p=0"
		challenge = basic:realm=\"example.com\"
		token = basic:$USERPASS64
	EOF

	start_http_server --auth-config="$TRASH_DIRECTORY/auth.config" &&

	set_credential_reply get <<-EOF &&
	protocol=http
	host=$HOST_PORT
	username=alice
	password=invalid-passwd
	EOF

	test_must_fail git -c "credential.helper=!\"$CREDENTIAL_HELPER\"" ls-remote $ORIGIN_URL &&

	expect_credential_query get <<-EOF &&
	protocol=http
	host=$HOST_PORT
	wwwauth[]=bearer authority="id.example.com" q=1 p=0
	wwwauth[]=basic realm="example.com"
	EOF

	expect_credential_query erase <<-EOF
	protocol=http
	host=$HOST_PORT
	username=alice
	password=invalid-passwd
	wwwauth[]=bearer authority="id.example.com" q=1 p=0
	wwwauth[]=basic realm="example.com"
	EOF
'

test_done
