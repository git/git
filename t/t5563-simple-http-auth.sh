#!/bin/sh

test_description='test http auth header and credential helper interop'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-httpd.sh

enable_cgipassauth
if ! test_have_prereq CGIPASSAUTH
then
	skip_all="no CGIPassAuth support"
	test_done
fi
start_httpd

test_expect_success 'setup_credential_helper' '
	mkdir "$TRASH_DIRECTORY/bin" &&
	PATH=$PATH:"$TRASH_DIRECTORY/bin" &&
	export PATH &&

	CREDENTIAL_HELPER="$TRASH_DIRECTORY/bin/git-credential-test-helper" &&
	write_script "$CREDENTIAL_HELPER" <<-\EOF
	cmd=$1
	teefile=$cmd-query-temp.cred
	catfile=$cmd-reply.cred
	sed -n -e "/^$/q" -e "p" >>$teefile
	state=$(sed -ne "s/^state\[\]=helper://p" "$teefile")
	if test -z "$state"
	then
		mv "$teefile" "$cmd-query.cred"
	else
		mv "$teefile" "$cmd-query-$state.cred"
		catfile="$cmd-reply-$state.cred"
	fi
	if test "$cmd" = "get"
	then
		cat $catfile
	fi
	EOF
'

set_credential_reply () {
	local suffix="$(test -n "$2" && echo "-$2")"
	cat >"$TRASH_DIRECTORY/$1-reply$suffix.cred"
}

expect_credential_query () {
	local suffix="$(test -n "$2" && echo "-$2")"
	cat >"$TRASH_DIRECTORY/$1-expect$suffix.cred" &&
	test_cmp "$TRASH_DIRECTORY/$1-expect$suffix.cred" \
		 "$TRASH_DIRECTORY/$1-query$suffix.cred"
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
	id=1 creds=Basic YWxpY2U6c2VjcmV0LXBhc3N3ZA==
	EOF

	cat >"$HTTPD_ROOT_PATH/custom-auth.challenge" <<-EOF &&
	id=1 status=200
	id=default response=WWW-Authenticate: Basic realm="example.com"
	EOF

	test_config_global credential.helper test-helper &&
	git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	capability[]=authtype
	capability[]=state
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

test_expect_success 'access using basic auth via authtype' '
	test_when_finished "per_test_cleanup" &&

	set_credential_reply get <<-EOF &&
	capability[]=authtype
	authtype=Basic
	credential=YWxpY2U6c2VjcmV0LXBhc3N3ZA==
	EOF

	# Basic base64(alice:secret-passwd)
	cat >"$HTTPD_ROOT_PATH/custom-auth.valid" <<-EOF &&
	id=1 creds=Basic YWxpY2U6c2VjcmV0LXBhc3N3ZA==
	EOF

	cat >"$HTTPD_ROOT_PATH/custom-auth.challenge" <<-EOF &&
	id=1 status=200
	id=default response=WWW-Authenticate: Basic realm="example.com"
	EOF

	test_config_global credential.helper test-helper &&
	GIT_CURL_VERBOSE=1 git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	capability[]=authtype
	capability[]=state
	protocol=http
	host=$HTTPD_DEST
	wwwauth[]=Basic realm="example.com"
	EOF

	expect_credential_query store <<-EOF
	capability[]=authtype
	authtype=Basic
	credential=YWxpY2U6c2VjcmV0LXBhc3N3ZA==
	protocol=http
	host=$HTTPD_DEST
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
	id=1 creds=Basic YWxpY2U6c2VjcmV0LXBhc3N3ZA==
	EOF

	cat >"$HTTPD_ROOT_PATH/custom-auth.challenge" <<-EOF &&
	id=1 status=200
	id=default response=WWW-Authenticate: Basic realm="example.com"
	EOF

	test_config_global credential.helper test-helper &&
	test_must_fail git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	capability[]=authtype
	capability[]=state
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
	id=1 creds=Basic YWxpY2U6c2VjcmV0LXBhc3N3ZA==
	EOF

	cat >"$HTTPD_ROOT_PATH/custom-auth.challenge" <<-EOF &&
	id=1 status=200
	id=default response=WWW-Authenticate: FooBar param1="value1" param2="value2"
	id=default response=WWW-Authenticate: Bearer authorize_uri="id.example.com" p=1 q=0
	id=default response=WWW-Authenticate: Basic realm="example.com"
	EOF

	test_config_global credential.helper test-helper &&
	git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	capability[]=authtype
	capability[]=state
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
	id=1 creds=Basic YWxpY2U6c2VjcmV0LXBhc3N3ZA==
	EOF

	cat >"$HTTPD_ROOT_PATH/custom-auth.challenge" <<-EOF &&
	id=1 status=200
	id=default response=www-authenticate: foobar param1="value1" param2="value2"
	id=default response=WWW-AUTHENTICATE: BEARER authorize_uri="id.example.com" p=1 q=0
	id=default response=WwW-aUtHeNtIcAtE: baSiC realm="example.com"
	EOF

	test_config_global credential.helper test-helper &&
	git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	capability[]=authtype
	capability[]=state
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
	id=1 creds=Basic YWxpY2U6c2VjcmV0LXBhc3N3ZA==
	EOF

	# Note that leading and trailing whitespace is important to correctly
	# simulate a continuation/folded header.
	cat >"$HTTPD_ROOT_PATH/custom-auth.challenge" <<-EOF &&
	id=1 status=200
	id=default response=WWW-Authenticate: FooBar param1="value1"
	id=default response= param2="value2"
	id=default response=WWW-Authenticate: Bearer authorize_uri="id.example.com"
	id=default response= p=1
	id=default response= q=0
	id=default response=WWW-Authenticate: Basic realm="example.com"
	EOF

	test_config_global credential.helper test-helper &&
	git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	capability[]=authtype
	capability[]=state
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
	id=1 creds=Basic YWxpY2U6c2VjcmV0LXBhc3N3ZA==
	EOF

	CHALLENGE="$HTTPD_ROOT_PATH/custom-auth.challenge" &&

	# Note that leading and trailing whitespace is important to correctly
	# simulate a continuation/folded header.
	printf "id=1 status=200\n" >"$CHALLENGE" &&
	printf "id=default response=WWW-Authenticate: FooBar param1=\"value1\"\r\n" >>"$CHALLENGE" &&
	printf "id=default response= \r\n" >>"$CHALLENGE" &&
	printf "id=default response= param2=\"value2\"\r\n" >>"$CHALLENGE" &&
	printf "id=default response=WWW-Authenticate: Bearer authorize_uri=\"id.example.com\"\r\n" >>"$CHALLENGE" &&
	printf "id=default response= p=1\r\n" >>"$CHALLENGE" &&
	printf "id=default response= \r\n" >>"$CHALLENGE" &&
	printf "id=default response= q=0\r\n" >>"$CHALLENGE" &&
	printf "id=default response=WWW-Authenticate: Basic realm=\"example.com\"\r\n" >>"$CHALLENGE" &&

	test_config_global credential.helper test-helper &&
	git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	capability[]=authtype
	capability[]=state
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
	id=1 creds=Basic YWxpY2U6c2VjcmV0LXBhc3N3ZA==
	EOF

	CHALLENGE="$HTTPD_ROOT_PATH/custom-auth.challenge" &&

	# Note that leading and trailing whitespace is important to correctly
	# simulate a continuation/folded header.
	printf "id=1 status=200\n" >"$CHALLENGE" &&
	printf "id=default response=WWW-Authenticate: FooBar param1=\"value1\"\r\n" >>"$CHALLENGE" &&
	printf "id=default response= \r\n" >>"$CHALLENGE" &&
	printf "id=default response=\tparam2=\"value2\"\r\n" >>"$CHALLENGE" &&
	printf "id=default response=WWW-Authenticate: Basic realm=\"example.com\"" >>"$CHALLENGE" &&

	test_config_global credential.helper test-helper &&
	git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	capability[]=authtype
	capability[]=state
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

test_expect_success 'access using bearer auth' '
	test_when_finished "per_test_cleanup" &&

	set_credential_reply get <<-EOF &&
	capability[]=authtype
	authtype=Bearer
	credential=YS1naXQtdG9rZW4=
	EOF

	# Basic base64(a-git-token)
	cat >"$HTTPD_ROOT_PATH/custom-auth.valid" <<-EOF &&
	id=1 creds=Bearer YS1naXQtdG9rZW4=
	EOF

	CHALLENGE="$HTTPD_ROOT_PATH/custom-auth.challenge" &&

	cat >"$HTTPD_ROOT_PATH/custom-auth.challenge" <<-EOF &&
	id=1 status=200
	id=default response=WWW-Authenticate: FooBar param1="value1" param2="value2"
	id=default response=WWW-Authenticate: Bearer authorize_uri="id.example.com" p=1 q=0
	id=default response=WWW-Authenticate: Basic realm="example.com"
	EOF

	test_config_global credential.helper test-helper &&
	git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	capability[]=authtype
	capability[]=state
	protocol=http
	host=$HTTPD_DEST
	wwwauth[]=FooBar param1="value1" param2="value2"
	wwwauth[]=Bearer authorize_uri="id.example.com" p=1 q=0
	wwwauth[]=Basic realm="example.com"
	EOF

	expect_credential_query store <<-EOF
	capability[]=authtype
	authtype=Bearer
	credential=YS1naXQtdG9rZW4=
	protocol=http
	host=$HTTPD_DEST
	EOF
'

test_expect_success 'access using bearer auth with invalid credentials' '
	test_when_finished "per_test_cleanup" &&

	set_credential_reply get <<-EOF &&
	capability[]=authtype
	authtype=Bearer
	credential=incorrect-token
	EOF

	# Basic base64(a-git-token)
	cat >"$HTTPD_ROOT_PATH/custom-auth.valid" <<-EOF &&
	id=1 creds=Bearer YS1naXQtdG9rZW4=
	EOF

	CHALLENGE="$HTTPD_ROOT_PATH/custom-auth.challenge" &&

	cat >"$HTTPD_ROOT_PATH/custom-auth.challenge" <<-EOF &&
	id=1 status=200
	id=default response=WWW-Authenticate: FooBar param1="value1" param2="value2"
	id=default response=WWW-Authenticate: Bearer authorize_uri="id.example.com" p=1 q=0
	id=default response=WWW-Authenticate: Basic realm="example.com"
	EOF

	test_config_global credential.helper test-helper &&
	test_must_fail git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	capability[]=authtype
	capability[]=state
	protocol=http
	host=$HTTPD_DEST
	wwwauth[]=FooBar param1="value1" param2="value2"
	wwwauth[]=Bearer authorize_uri="id.example.com" p=1 q=0
	wwwauth[]=Basic realm="example.com"
	EOF

	expect_credential_query erase <<-EOF
	capability[]=authtype
	authtype=Bearer
	credential=incorrect-token
	protocol=http
	host=$HTTPD_DEST
	wwwauth[]=FooBar param1="value1" param2="value2"
	wwwauth[]=Bearer authorize_uri="id.example.com" p=1 q=0
	wwwauth[]=Basic realm="example.com"
	EOF
'

test_expect_success 'access using three-legged auth' '
	test_when_finished "per_test_cleanup" &&

	set_credential_reply get <<-EOF &&
	capability[]=authtype
	capability[]=state
	authtype=Multistage
	credential=YS1naXQtdG9rZW4=
	state[]=helper:foobar
	continue=1
	EOF

	set_credential_reply get foobar <<-EOF &&
	capability[]=authtype
	capability[]=state
	authtype=Multistage
	credential=YW5vdGhlci10b2tlbg==
	state[]=helper:bazquux
	EOF

	cat >"$HTTPD_ROOT_PATH/custom-auth.valid" <<-EOF &&
	id=1 creds=Multistage YS1naXQtdG9rZW4=
	id=2 creds=Multistage YW5vdGhlci10b2tlbg==
	EOF

	CHALLENGE="$HTTPD_ROOT_PATH/custom-auth.challenge" &&

	cat >"$HTTPD_ROOT_PATH/custom-auth.challenge" <<-EOF &&
	id=1 status=401 response=WWW-Authenticate: Multistage challenge="456"
	id=1 status=401 response=WWW-Authenticate: Bearer authorize_uri="id.example.com" p=1 q=0
	id=2 status=200
	id=default response=WWW-Authenticate: Multistage challenge="123"
	id=default response=WWW-Authenticate: Bearer authorize_uri="id.example.com" p=1 q=0
	EOF

	test_config_global credential.helper test-helper &&
	git ls-remote "$HTTPD_URL/custom_auth/repo.git" &&

	expect_credential_query get <<-EOF &&
	capability[]=authtype
	capability[]=state
	protocol=http
	host=$HTTPD_DEST
	wwwauth[]=Multistage challenge="123"
	wwwauth[]=Bearer authorize_uri="id.example.com" p=1 q=0
	EOF

	expect_credential_query get foobar <<-EOF &&
	capability[]=authtype
	capability[]=state
	authtype=Multistage
	protocol=http
	host=$HTTPD_DEST
	wwwauth[]=Multistage challenge="456"
	wwwauth[]=Bearer authorize_uri="id.example.com" p=1 q=0
	state[]=helper:foobar
	EOF

	expect_credential_query store bazquux <<-EOF
	capability[]=authtype
	capability[]=state
	authtype=Multistage
	credential=YW5vdGhlci10b2tlbg==
	protocol=http
	host=$HTTPD_DEST
	state[]=helper:bazquux
	EOF
'

test_done
