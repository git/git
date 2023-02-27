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
	EOF

	expect_credential_query store <<-EOF
	protocol=http
	host=$HTTPD_DEST
	username=alice
	password=secret-passwd
	EOF
'

test_done
