#!/bin/sh

test_description='test http auth header and credential helper interop'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-httpd.sh

start_httpd

test_expect_success 'setup_credential_helper' '
	mkdir "$TRASH_DIRECTORY/bin" &&
	PATH=$PATH:"$TRASH_DIRECTORY/bin" &&
	export PATH &&

	CREDENTIAL_HELPER="$TRASH_DIRECTORY/bin/git-credential-test-helper" &&
	write_script "$CREDENTIAL_HELPER" <<-\EOF
	cmd=$1
	teefile=$cmd-query.cred
	catfile=$cmd-reply.cred
	sed -n -e "/^$/q" -e "p" >>$teefile
	if test "$cmd" = "get"
	then
		cat $catfile
	fi
	EOF
'

set_credential_reply () {
	cat >"$TRASH_DIRECTORY/$1-reply.cred"
}

expect_credential_query () {
	cat >"$TRASH_DIRECTORY/$1-expect.cred" &&
	test_cmp "$TRASH_DIRECTORY/$1-expect.cred" \
		 "$TRASH_DIRECTORY/$1-query.cred"
}

per_test_cleanup () {
	rm -f *.cred &&
	rm -f "$HTTPD_ROOT_PATH"/custom-auth.valid \
	      "$HTTPD_ROOT_PATH"/custom-auth.challenge
}

test_expect_success 'setup repository' '
	test_commit foo &&
	git init --bare "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	git push --mirror "$HTTPD_DOCUMENT_ROOT_PATH/repo.git"
'

test_expect_success 'access using basic auth' '
	test_when_finished "per_test_cleanup" &&

	set_credential_reply get <<-EOF &&
	username=alice
	password=secret-passwd
	EOF

	# Basic base64(alice:secret-passwd)
	cat >"$HTTPD_ROOT_PATH/custom-auth.valid" <<-EOF &&
	Basic YWxpY2U6c2VjcmV0LXBhc3N3ZA==
	EOF

	cat >"$HTTPD_ROOT_PATH/custom-auth.challenge" <<-EOF &&
	WWW-Authenticate: Basic realm="example.com"
	EOF

	test_config_global credential.helper test-helper &&
	git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	protocol=http
	host=$HTTPD_DEST
	wwwauth[]=Basic realm="example.com"
	EOF

	expect_credential_query store <<-EOF
	protocol=http
	host=$HTTPD_DEST
	username=alice
	password=secret-passwd
	EOF
'

test_expect_success 'access using basic auth invalid credentials' '
	test_when_finished "per_test_cleanup" &&

	set_credential_reply get <<-EOF &&
	username=baduser
	password=wrong-passwd
	EOF

	# Basic base64(alice:secret-passwd)
	cat >"$HTTPD_ROOT_PATH/custom-auth.valid" <<-EOF &&
	Basic YWxpY2U6c2VjcmV0LXBhc3N3ZA==
	EOF

	cat >"$HTTPD_ROOT_PATH/custom-auth.challenge" <<-EOF &&
	WWW-Authenticate: Basic realm="example.com"
	EOF

	test_config_global credential.helper test-helper &&
	test_must_fail git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	protocol=http
	host=$HTTPD_DEST
	wwwauth[]=Basic realm="example.com"
	EOF

	expect_credential_query erase <<-EOF
	protocol=http
	host=$HTTPD_DEST
	username=baduser
	password=wrong-passwd
	wwwauth[]=Basic realm="example.com"
	EOF
'

test_expect_success 'access using basic auth with extra challenges' '
	test_when_finished "per_test_cleanup" &&

	set_credential_reply get <<-EOF &&
	username=alice
	password=secret-passwd
	EOF

	# Basic base64(alice:secret-passwd)
	cat >"$HTTPD_ROOT_PATH/custom-auth.valid" <<-EOF &&
	Basic YWxpY2U6c2VjcmV0LXBhc3N3ZA==
	EOF

	cat >"$HTTPD_ROOT_PATH/custom-auth.challenge" <<-EOF &&
	WWW-Authenticate: FooBar param1="value1" param2="value2"
	WWW-Authenticate: Bearer authorize_uri="id.example.com" p=1 q=0
	WWW-Authenticate: Basic realm="example.com"
	EOF

	test_config_global credential.helper test-helper &&
	git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	protocol=http
	host=$HTTPD_DEST
	wwwauth[]=FooBar param1="value1" param2="value2"
	wwwauth[]=Bearer authorize_uri="id.example.com" p=1 q=0
	wwwauth[]=Basic realm="example.com"
	EOF

	expect_credential_query store <<-EOF
	protocol=http
	host=$HTTPD_DEST
	username=alice
	password=secret-passwd
	EOF
'

test_expect_success 'access using basic auth mixed-case wwwauth header name' '
	test_when_finished "per_test_cleanup" &&

	set_credential_reply get <<-EOF &&
	username=alice
	password=secret-passwd
	EOF

	# Basic base64(alice:secret-passwd)
	cat >"$HTTPD_ROOT_PATH/custom-auth.valid" <<-EOF &&
	Basic YWxpY2U6c2VjcmV0LXBhc3N3ZA==
	EOF

	cat >"$HTTPD_ROOT_PATH/custom-auth.challenge" <<-EOF &&
	www-authenticate: foobar param1="value1" param2="value2"
	WWW-AUTHENTICATE: BEARER authorize_uri="id.example.com" p=1 q=0
	WwW-aUtHeNtIcAtE: baSiC realm="example.com"
	EOF

	test_config_global credential.helper test-helper &&
	git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	protocol=http
	host=$HTTPD_DEST
	wwwauth[]=foobar param1="value1" param2="value2"
	wwwauth[]=BEARER authorize_uri="id.example.com" p=1 q=0
	wwwauth[]=baSiC realm="example.com"
	EOF

	expect_credential_query store <<-EOF
	protocol=http
	host=$HTTPD_DEST
	username=alice
	password=secret-passwd
	EOF
'

test_expect_success 'access using basic auth with wwwauth header continuations' '
	test_when_finished "per_test_cleanup" &&

	set_credential_reply get <<-EOF &&
	username=alice
	password=secret-passwd
	EOF

	# Basic base64(alice:secret-passwd)
	cat >"$HTTPD_ROOT_PATH/custom-auth.valid" <<-EOF &&
	Basic YWxpY2U6c2VjcmV0LXBhc3N3ZA==
	EOF

	# Note that leading and trailing whitespace is important to correctly
	# simulate a continuation/folded header.
	cat >"$HTTPD_ROOT_PATH/custom-auth.challenge" <<-EOF &&
	WWW-Authenticate: FooBar param1="value1"
	 param2="value2"
	WWW-Authenticate: Bearer authorize_uri="id.example.com"
	 p=1
	 q=0
	WWW-Authenticate: Basic realm="example.com"
	EOF

	test_config_global credential.helper test-helper &&
	git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	protocol=http
	host=$HTTPD_DEST
	wwwauth[]=FooBar param1="value1" param2="value2"
	wwwauth[]=Bearer authorize_uri="id.example.com" p=1 q=0
	wwwauth[]=Basic realm="example.com"
	EOF

	expect_credential_query store <<-EOF
	protocol=http
	host=$HTTPD_DEST
	username=alice
	password=secret-passwd
	EOF
'

test_expect_success 'access using basic auth with wwwauth header empty continuations' '
	test_when_finished "per_test_cleanup" &&

	set_credential_reply get <<-EOF &&
	username=alice
	password=secret-passwd
	EOF

	# Basic base64(alice:secret-passwd)
	cat >"$HTTPD_ROOT_PATH/custom-auth.valid" <<-EOF &&
	Basic YWxpY2U6c2VjcmV0LXBhc3N3ZA==
	EOF

	CHALLENGE="$HTTPD_ROOT_PATH/custom-auth.challenge" &&

	# Note that leading and trailing whitespace is important to correctly
	# simulate a continuation/folded header.
	printf "WWW-Authenticate: FooBar param1=\"value1\"\r\n" >"$CHALLENGE" &&
	printf " \r\n" >>"$CHALLENGE" &&
	printf " param2=\"value2\"\r\n" >>"$CHALLENGE" &&
	printf "WWW-Authenticate: Bearer authorize_uri=\"id.example.com\"\r\n" >>"$CHALLENGE" &&
	printf " p=1\r\n" >>"$CHALLENGE" &&
	printf " \r\n" >>"$CHALLENGE" &&
	printf " q=0\r\n" >>"$CHALLENGE" &&
	printf "WWW-Authenticate: Basic realm=\"example.com\"\r\n" >>"$CHALLENGE" &&

	test_config_global credential.helper test-helper &&
	git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	protocol=http
	host=$HTTPD_DEST
	wwwauth[]=FooBar param1="value1" param2="value2"
	wwwauth[]=Bearer authorize_uri="id.example.com" p=1 q=0
	wwwauth[]=Basic realm="example.com"
	EOF

	expect_credential_query store <<-EOF
	protocol=http
	host=$HTTPD_DEST
	username=alice
	password=secret-passwd
	EOF
'

test_expect_success 'access using basic auth with wwwauth header mixed line-endings' '
	test_when_finished "per_test_cleanup" &&

	set_credential_reply get <<-EOF &&
	username=alice
	password=secret-passwd
	EOF

	# Basic base64(alice:secret-passwd)
	cat >"$HTTPD_ROOT_PATH/custom-auth.valid" <<-EOF &&
	Basic YWxpY2U6c2VjcmV0LXBhc3N3ZA==
	EOF

	CHALLENGE="$HTTPD_ROOT_PATH/custom-auth.challenge" &&

	# Note that leading and trailing whitespace is important to correctly
	# simulate a continuation/folded header.
	printf "WWW-Authenticate: FooBar param1=\"value1\"\r\n" >"$CHALLENGE" &&
	printf " \r\n" >>"$CHALLENGE" &&
	printf "\tparam2=\"value2\"\r\n" >>"$CHALLENGE" &&
	printf "WWW-Authenticate: Basic realm=\"example.com\"" >>"$CHALLENGE" &&

	test_config_global credential.helper test-helper &&
	git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	protocol=http
	host=$HTTPD_DEST
	wwwauth[]=FooBar param1="value1" param2="value2"
	wwwauth[]=Basic realm="example.com"
	EOF

	expect_credential_query store <<-EOF
	protocol=http
	host=$HTTPD_DEST
	username=alice
	password=secret-passwd
	EOF
'

test_done
