#!/bin/sh

test_description='basic credential helper tests'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-credential.sh

test_expect_success 'setup helper scripts' '
	cat >dump <<-\EOF &&
	whoami=$(echo $0 | sed s/.*git-credential-//)
	echo >&2 "$whoami: $*"
	OIFS=$IFS
	IFS==
	while read key value; do
		echo >&2 "$whoami: $key=$value"
		eval "$key=$value"
	done
	IFS=$OIFS
	EOF

	write_script git-credential-useless <<-\EOF &&
	. ./dump
	exit 0
	EOF

	write_script git-credential-verbatim <<-\EOF &&
	user=$1; shift
	pass=$1; shift
	. ./dump
	test -z "$user" || echo username=$user
	test -z "$pass" || echo password=$pass
	EOF

	PATH="$PWD:$PATH"
'

test_expect_success 'credential_fill invokes helper' '
	check fill "verbatim foo bar" <<-\EOF
	--
	username=foo
	password=bar
	--
	verbatim: get
	EOF
'

test_expect_success 'credential_fill invokes multiple helpers' '
	check fill useless "verbatim foo bar" <<-\EOF
	--
	username=foo
	password=bar
	--
	useless: get
	verbatim: get
	EOF
'

test_expect_success 'credential_fill stops when we get a full response' '
	check fill "verbatim one two" "verbatim three four" <<-\EOF
	--
	username=one
	password=two
	--
	verbatim: get
	EOF
'

test_expect_success 'credential_fill continues through partial response' '
	check fill "verbatim one \"\"" "verbatim two three" <<-\EOF
	--
	username=two
	password=three
	--
	verbatim: get
	verbatim: get
	verbatim: username=one
	EOF
'

test_expect_success 'credential_fill passes along metadata' '
	check fill "verbatim one two" <<-\EOF
	protocol=ftp
	host=example.com
	path=foo.git
	--
	protocol=ftp
	host=example.com
	path=foo.git
	username=one
	password=two
	--
	verbatim: get
	verbatim: protocol=ftp
	verbatim: host=example.com
	verbatim: path=foo.git
	EOF
'

test_expect_success 'credential_approve calls all helpers' '
	check approve useless "verbatim one two" <<-\EOF
	username=foo
	password=bar
	--
	--
	useless: store
	useless: username=foo
	useless: password=bar
	verbatim: store
	verbatim: username=foo
	verbatim: password=bar
	EOF
'

test_expect_success 'do not bother storing password-less credential' '
	check approve useless <<-\EOF
	username=foo
	--
	--
	EOF
'


test_expect_success 'credential_reject calls all helpers' '
	check reject useless "verbatim one two" <<-\EOF
	username=foo
	password=bar
	--
	--
	useless: erase
	useless: username=foo
	useless: password=bar
	verbatim: erase
	verbatim: username=foo
	verbatim: password=bar
	EOF
'

test_expect_success 'usernames can be preserved' '
	check fill "verbatim \"\" three" <<-\EOF
	username=one
	--
	username=one
	password=three
	--
	verbatim: get
	verbatim: username=one
	EOF
'

test_expect_success 'usernames can be overridden' '
	check fill "verbatim two three" <<-\EOF
	username=one
	--
	username=two
	password=three
	--
	verbatim: get
	verbatim: username=one
	EOF
'

test_expect_success 'do not bother completing already-full credential' '
	check fill "verbatim three four" <<-\EOF
	username=one
	password=two
	--
	username=one
	password=two
	--
	EOF
'

# We can't test the basic terminal password prompt here because
# getpass() tries too hard to find the real terminal. But if our
# askpass helper is run, we know the internal getpass is working.
test_expect_success 'empty helper list falls back to internal getpass' '
	check fill <<-\EOF
	--
	username=askpass-username
	password=askpass-password
	--
	askpass: Username:
	askpass: Password:
	EOF
'

test_expect_success 'internal getpass does not ask for known username' '
	check fill <<-\EOF
	username=foo
	--
	username=foo
	password=askpass-password
	--
	askpass: Password:
	EOF
'

HELPER="!f() {
		cat >/dev/null
		echo username=foo
		echo password=bar
	}; f"
test_expect_success 'respect configured credentials' '
	test_config credential.helper "$HELPER" &&
	check fill <<-\EOF
	--
	username=foo
	password=bar
	--
	EOF
'

test_expect_success 'match configured credential' '
	test_config credential.https://example.com.helper "$HELPER" &&
	check fill <<-\EOF
	protocol=https
	host=example.com
	path=repo.git
	--
	protocol=https
	host=example.com
	username=foo
	password=bar
	--
	EOF
'

test_expect_success 'do not match configured credential' '
	test_config credential.https://foo.helper "$HELPER" &&
	check fill <<-\EOF
	protocol=https
	host=bar
	--
	protocol=https
	host=bar
	username=askpass-username
	password=askpass-password
	--
	askpass: Username for '\''https://bar'\'':
	askpass: Password for '\''https://askpass-username@bar'\'':
	EOF
'

test_expect_success 'pull username from config' '
	test_config credential.https://example.com.username foo &&
	check fill <<-\EOF
	protocol=https
	host=example.com
	--
	protocol=https
	host=example.com
	username=foo
	password=askpass-password
	--
	askpass: Password for '\''https://foo@example.com'\'':
	EOF
'

test_expect_success 'http paths can be part of context' '
	check fill "verbatim foo bar" <<-\EOF &&
	protocol=https
	host=example.com
	path=foo.git
	--
	protocol=https
	host=example.com
	username=foo
	password=bar
	--
	verbatim: get
	verbatim: protocol=https
	verbatim: host=example.com
	EOF
	test_config credential.https://example.com.useHttpPath true &&
	check fill "verbatim foo bar" <<-\EOF
	protocol=https
	host=example.com
	path=foo.git
	--
	protocol=https
	host=example.com
	path=foo.git
	username=foo
	password=bar
	--
	verbatim: get
	verbatim: protocol=https
	verbatim: host=example.com
	verbatim: path=foo.git
	EOF
'

test_done
